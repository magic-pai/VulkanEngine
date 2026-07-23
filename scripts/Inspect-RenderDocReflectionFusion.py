import json
import os
import traceback

import renderdoc as rd


CAPTURE_PATH = os.environ.get("SE_RENDERDOC_CAPTURE_PATH", "")
OUTPUT_PATH = os.environ.get("SE_RENDERDOC_INSPECTION_PATH", "")


def flatten_actions(actions, structured_file, path=""):
    flattened = []
    for action in actions:
        name = str(action.GetName(structured_file))
        action_path = name if not path else path + "/" + name
        flattened.append((action, action_path))
        flattened.extend(
            flatten_actions(action.children, structured_file, action_path)
        )
    return flattened


def descriptor_records(controller, resource_names):
    records = []
    for access in controller.GetDescriptorAccess():
        descriptor_range = rd.DescriptorRange()
        descriptor_range.offset = access.byteOffset
        descriptor_range.descriptorSize = access.byteSize
        descriptor_range.count = 1
        descriptor_range.type = access.type
        descriptors = controller.GetDescriptors(
            access.descriptorStore, [descriptor_range]
        )
        locations = controller.GetDescriptorLocations(
            access.descriptorStore, [descriptor_range]
        )
        if not descriptors or not locations:
            continue
        descriptor = descriptors[0]
        resource = str(descriptor.resource)
        records.append({
            "binding": str(locations[0].logicalBindName),
            "stage": str(access.stage),
            "type": str(access.type),
            "arrayElement": int(access.arrayElement),
            "resourceName": resource_names.get(resource, ""),
            "resource": resource,
            "view": str(descriptor.view),
            "secondary": str(descriptor.secondary),
            "firstMip": int(descriptor.firstMip),
            "numMips": int(descriptor.numMips),
            "firstSlice": int(descriptor.firstSlice),
            "numSlices": int(descriptor.numSlices),
        })
    return records


def add_check(checks, name, passed, actual, expected):
    checks.append({
        "name": name,
        "status": "pass" if passed else "fail",
        "actual": actual,
        "expected": expected,
    })


def binding_map(records):
    return {record["binding"]: record for record in records}


report = {
    "contractVersion": 1,
    "status": "starting",
    "capturePath": CAPTURE_PATH,
    "checks": [],
}
controller = None
capture = None
exit_code = 1
try:
    if not CAPTURE_PATH or not OUTPUT_PATH:
        raise RuntimeError("RenderDoc capture/output environment is missing")

    rd.InitialiseReplay(rd.GlobalEnvironment(), [])
    capture = rd.OpenCaptureFile()
    open_result = capture.OpenFile(CAPTURE_PATH, "", None)
    if open_result != rd.ResultCode.Succeeded:
        raise RuntimeError("OpenFile failed: " + str(open_result))
    if not capture.LocalReplaySupport():
        raise RuntimeError("Capture has no local replay support")
    replay_result, controller = capture.OpenCapture(rd.ReplayOptions(), None)
    if replay_result != rd.ResultCode.Succeeded:
        raise RuntimeError("OpenCapture failed: " + str(replay_result))

    structured_file = controller.GetStructuredFile()
    actions = flatten_actions(controller.GetRootActions(), structured_file)
    resources = controller.GetResources()
    resource_names = {str(item.resourceId): item.name for item in resources}

    apply_events = []
    ray_query_events = []
    for action, path in actions:
        flags = str(action.flags)
        is_dispatch = "Dispatch" in flags
        is_apply = (
            "SelfEngine.Reflection.Apply" in path and
            "Drawcall" in flags
        )
        if not is_dispatch and not is_apply:
            continue
        if not is_apply and "SelfEngine.Reflection.FFX.Intersect" not in path:
            continue

        controller.SetFrameEvent(action.eventId, True)
        records = descriptor_records(controller, resource_names)
        event = {
            "eventId": int(action.eventId),
            "path": path,
            "descriptors": records,
        }
        if is_apply:
            pipeline_state = controller.GetPipelineState()
            color_blends = pipeline_state.GetColorBlends()
            first_blend = color_blends[0] if color_blends else None
            event["colorBlend"] = None if first_blend is None else {
                "enabled": bool(first_blend.enabled),
                "colorSource": str(first_blend.colorBlend.source),
                "colorDestination": str(first_blend.colorBlend.destination),
                "colorOperation": str(first_blend.colorBlend.operation),
                "alphaSource": str(first_blend.alphaBlend.source),
                "alphaDestination": str(first_blend.alphaBlend.destination),
                "alphaOperation": str(first_blend.alphaBlend.operation),
                "writeMask": str(first_blend.writeMask),
            }
            apply_events.append(event)
        bindings = {record["binding"].split("[")[0] for record in records}
        if is_dispatch and {"24", "25", "26"}.issubset(bindings):
            ray_query_events.append(event)

    checks = report["checks"]
    add_check(
        checks,
        "single Reflection Apply draw",
        len(apply_events) == 1,
        len(apply_events),
        1,
    )
    add_check(
        checks,
        "single Ray Query fusion dispatch",
        len(ray_query_events) == 1,
        len(ray_query_events),
        1,
    )

    if len(apply_events) == 1:
        apply_bindings = binding_map(apply_events[0]["descriptors"])
        expected_apply = {
            "16": "SelfEngine.Reflection.FFX.IntersectRadiance",
            "17": "SelfEngine.Reflection.FFX.RadianceHistory",
            "18": "SelfEngine.Reflection.FFX.HitConfidenceHistory",
        }
        for binding, expected_name in expected_apply.items():
            record = apply_bindings.get(binding)
            actual_name = "" if record is None else record["resourceName"]
            add_check(
                checks,
                "Apply binding " + binding,
                record is not None and expected_name in actual_name,
                actual_name,
                expected_name,
            )
        apply_views = [
            apply_bindings[binding]["view"]
            for binding in expected_apply
            if binding in apply_bindings
        ]
        add_check(
            checks,
            "Apply carriers use distinct views",
            len(apply_views) == 3 and len(set(apply_views)) == 3,
            apply_views,
            "3 non-aliased image views",
        )
        blend = apply_events[0]["colorBlend"]
        expected_blend = {
            "enabled": True,
            "colorSource": "BlendMultiplier.DstAlpha",
            "colorDestination": "BlendMultiplier.One",
            "colorOperation": "BlendOperation.Add",
        }
        blend_matches = (
            blend is not None and
            all(blend.get(key) == value
                for key, value in expected_blend.items())
        )
        add_check(
            checks,
            "Apply uses visibility-owned destination-alpha replacement",
            blend_matches,
            blend,
            expected_blend,
        )

    if len(ray_query_events) == 1:
        records = ray_query_events[0]["descriptors"]
        spatial = [record for record in records if record["binding"] == "26"]
        prefiltered = [
            record for record in records
            if record["binding"].startswith("24[")
        ]
        diffuse = [
            record for record in records
            if record["binding"].startswith("25[")
        ]
        add_check(
            checks,
            "Ray Query spatial Probe buffer",
            len(spatial) == 1 and
            spatial[0]["resource"] != "ResourceId::0" and
            "Buffer" in spatial[0]["resourceName"],
            spatial,
            "binding 26 non-null buffer",
        )
        add_check(
            checks,
            "Ray Query local prefiltered Probe views",
            len(prefiltered) > 0 and
            all(item["numMips"] > 1 and item["numSlices"] == 6
                for item in prefiltered) and
            len({item["view"] for item in prefiltered}) == len(prefiltered),
            prefiltered,
            "binding 24 distinct 6-face multi-mip views",
        )
        add_check(
            checks,
            "Ray Query local diffuse Probe views",
            len(diffuse) > 0 and
            all(item["numMips"] == 1 and item["numSlices"] == 6
                for item in diffuse) and
            len({item["view"] for item in diffuse}) == len(diffuse),
            diffuse,
            "binding 25 distinct 6-face single-mip views",
        )
        prefiltered_elements = {
            item["arrayElement"] for item in prefiltered
        }
        diffuse_elements = {item["arrayElement"] for item in diffuse}
        add_check(
            checks,
            "Ray Query local Probe array identity",
            prefiltered_elements == diffuse_elements and
            len(prefiltered_elements) > 0,
            sorted(prefiltered_elements),
            "matching binding 24/25 array elements",
        )

    if len(apply_events) == 1:
        report["applyEvent"] = {
            "eventId": apply_events[0]["eventId"],
            "path": apply_events[0]["path"],
            "colorBlend": apply_events[0]["colorBlend"],
            "descriptors": [
                record for record in apply_events[0]["descriptors"]
                if record["binding"] in ("16", "17", "18")
            ],
        }
    else:
        report["applyEvent"] = None
    if len(ray_query_events) == 1:
        report["rayQueryEvent"] = {
            "eventId": ray_query_events[0]["eventId"],
            "path": ray_query_events[0]["path"],
            "descriptors": [
                record for record in ray_query_events[0]["descriptors"]
                if record["binding"] == "26" or
                record["binding"].startswith("24[") or
                record["binding"].startswith("25[")
            ],
        }
    else:
        report["rayQueryEvent"] = None
    report["passCount"] = sum(
        1 for check in checks if check["status"] == "pass"
    )
    report["failCount"] = sum(
        1 for check in checks if check["status"] == "fail"
    )
    report["status"] = "complete"
    exit_code = 0 if report["failCount"] == 0 else 1
except Exception:
    report["status"] = "error"
    report["error"] = traceback.format_exc()
finally:
    if controller is not None:
        controller.Shutdown()
    if capture is not None:
        capture.Shutdown()
    try:
        rd.ShutdownReplay()
    except Exception:
        pass
    if OUTPUT_PATH:
        with open(OUTPUT_PATH, "w", encoding="utf-8") as output:
            json.dump(report, output, indent=2)
    os._exit(exit_code)
