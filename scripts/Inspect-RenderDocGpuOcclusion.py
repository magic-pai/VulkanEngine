import json
import os
import traceback

import renderdoc as rd


CAPTURE_PATH = os.environ.get("SE_RENDERDOC_CAPTURE_PATH", "")
OUTPUT_PATH = os.environ.get("SE_RENDERDOC_INSPECTION_PATH", "")
PYRAMID_NAME = "SelfEngine.Occlusion.DepthPyramid"


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
        if str(access.stage) != "ShaderStage.Compute":
            continue
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
            "resourceName": resource_names.get(resource, ""),
            "resource": resource,
            "view": str(descriptor.view),
            "firstMip": int(descriptor.firstMip),
            "numMips": int(descriptor.numMips),
        })
    return records


def binding_map(records):
    return {record["binding"]: record for record in records}


def add_check(checks, name, passed, actual, expected):
    checks.append({
        "name": name,
        "status": "pass" if passed else "fail",
        "actual": actual,
        "expected": expected,
    })


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
    textures = {
        str(item.resourceId): item for item in controller.GetTextures()
    }

    build_dispatches = []
    classification_dispatches = []
    for action, path in actions:
        if "Dispatch" not in str(action.flags):
            continue
        controller.SetFrameEvent(action.eventId, True)
        descriptors = descriptor_records(controller, resource_names)
        dispatch_dimension = action.dispatchDimension
        event = {
            "eventId": int(action.eventId),
            "path": path,
            "dispatch": [
                int(dispatch_dimension[0]),
                int(dispatch_dimension[1]),
                int(dispatch_dimension[2]),
            ],
            "descriptors": descriptors,
        }
        if "GPU Occlusion Audit" in path:
            classification_dispatches.append(event)
            continue
        destination = binding_map(descriptors).get("1")
        if (
            destination is not None and
            PYRAMID_NAME in destination["resourceName"]
        ):
            build_dispatches.append(event)

    checks = report["checks"]
    add_check(
        checks,
        "single GPU occlusion classification dispatch",
        len(classification_dispatches) == 1,
        len(classification_dispatches),
        1,
    )

    classification = (
        classification_dispatches[0]
        if len(classification_dispatches) == 1
        else None
    )
    classification_bindings = (
        binding_map(classification["descriptors"])
        if classification is not None
        else {}
    )
    expected_bindings = {
        "0": PYRAMID_NAME,
        "1": "SelfEngine.GpuOcclusion.Candidates",
        "2": "SelfEngine.GpuOcclusion.Results",
    }
    for binding, expected_name in expected_bindings.items():
        record = classification_bindings.get(binding)
        actual_name = "" if record is None else record["resourceName"]
        add_check(
            checks,
            "classification binding " + binding,
            record is not None and expected_name in actual_name,
            actual_name,
            expected_name,
        )

    classification_resources = {
        record["resource"]
        for binding, record in classification_bindings.items()
        if binding in expected_bindings
    }
    add_check(
        checks,
        "classification resources do not alias",
        len(classification_resources) == 3,
        sorted(classification_resources),
        "3 distinct resources",
    )

    pyramid_record = classification_bindings.get("0")
    texture = None
    if pyramid_record is not None:
        texture = textures.get(pyramid_record["resource"])
    pyramid_mips = 0 if texture is None else int(texture.mips)
    pyramid_extent = [] if texture is None else [
        int(texture.width),
        int(texture.height),
    ]
    pyramid_format = "" if texture is None else texture.format.Name()
    add_check(
        checks,
        "occlusion depth pyramid texture contract",
        texture is not None and
        pyramid_extent[0] > 0 and
        pyramid_extent[1] > 0 and
        pyramid_mips > 1 and
        pyramid_format == "R32_FLOAT",
        {
            "extent": pyramid_extent,
            "mips": pyramid_mips,
            "format": pyramid_format,
        },
        "non-empty R32_SFLOAT texture with multiple mips",
    )

    destination_mips = sorted({
        binding_map(event["descriptors"])["1"]["firstMip"]
        for event in build_dispatches
    })
    add_check(
        checks,
        "one HZB build dispatch per mip",
        pyramid_mips > 0 and len(build_dispatches) == pyramid_mips,
        len(build_dispatches),
        pyramid_mips,
    )
    add_check(
        checks,
        "HZB build covers the full mip chain",
        destination_mips == list(range(pyramid_mips)),
        destination_mips,
        list(range(pyramid_mips)),
    )

    dispatch_dimensions = (
        [] if classification is None else classification["dispatch"]
    )
    add_check(
        checks,
        "classification dispatch dimensions are bounded",
        len(dispatch_dimensions) == 3 and
        dispatch_dimensions[0] > 0 and
        dispatch_dimensions[1:] == [1, 1],
        dispatch_dimensions,
        "x>0, y=1, z=1",
    )

    build_event_ids = [event["eventId"] for event in build_dispatches]
    classification_event_id = (
        0 if classification is None else classification["eventId"]
    )
    add_check(
        checks,
        "HZB build completes before classification",
        bool(build_event_ids) and
        classification_event_id > max(build_event_ids),
        {
            "build": build_event_ids,
            "classification": classification_event_id,
        },
        "all build event IDs precede classification",
    )

    report["pyramid"] = {
        "extent": pyramid_extent,
        "mips": pyramid_mips,
        "format": pyramid_format,
    }
    report["buildDispatches"] = build_dispatches
    report["classificationDispatch"] = classification
    report["passCount"] = sum(
        1 for check in checks if check["status"] == "pass"
    )
    report["failCount"] = sum(
        1 for check in checks if check["status"] == "fail"
    )
    report["status"] = "complete"
    exit_code = 0 if report["failCount"] == 0 else 2
except Exception as error:
    report["status"] = "error"
    report["error"] = str(error)
    report["traceback"] = traceback.format_exc()
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

raise SystemExit(exit_code)
