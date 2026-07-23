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
        flattened.append((action, action_path, name))
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
        })
    return records


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

    skinning_dispatches = []
    blas_actions = []
    for action, path, name in actions:
        flags = str(action.flags)
        if (
            "SelfEngine.Reflection.Skinned.Compute" in path and
            "Dispatch" in flags
        ):
            controller.SetFrameEvent(action.eventId, True)
            skinning_dispatches.append({
                "eventId": int(action.eventId),
                "path": path,
                "flags": flags,
                "descriptors": descriptor_records(controller, resource_names),
            })
        if "SelfEngine.Reflection.Skinned.BLAS" in path and (
            "Build" in flags or
            "BuildAcceleration" in name or
            "AccelerationStructure" in name
        ):
            blas_actions.append({
                "eventId": int(action.eventId),
                "path": path,
                "flags": flags,
            })

    checks = report["checks"]
    add_check(
        checks,
        "single GPU skinning dispatch",
        len(skinning_dispatches) == 1,
        len(skinning_dispatches),
        1,
    )
    add_check(
        checks,
        "dynamic BLAS build/update action recorded",
        len(blas_actions) >= 1,
        blas_actions,
        ">=1 action under SelfEngine.Reflection.Skinned.BLAS",
    )
    if len(skinning_dispatches) == 1:
        descriptors = skinning_dispatches[0]["descriptors"]
        skinned_vertices = [
            item for item in descriptors
            if "SelfEngine.Reflection.SkinnedVertices." in item["resourceName"]
        ]
        palette_snapshots = [
            item for item in descriptors
            if "SelfEngine.Reflection.SkinnedPalette." in item["resourceName"]
        ]
        add_check(
            checks,
            "skinning dispatch binds its frame-slot vertex output",
            len(skinned_vertices) == 1,
            skinned_vertices,
            "one SelfEngine.Reflection.SkinnedVertices resource",
        )
        add_check(
            checks,
            "skinning dispatch binds its frame-slot palette snapshot",
            len(palette_snapshots) == 1,
            palette_snapshots,
            "one SelfEngine.Reflection.SkinnedPalette resource",
        )
        distinct_resources = {
            item["resource"]
            for item in skinned_vertices + palette_snapshots
        }
        add_check(
            checks,
            "skinned output and palette do not alias",
            len(distinct_resources) == 2,
            sorted(distinct_resources),
            "2 distinct buffers",
        )
        storage_buffers = [
            item for item in descriptors
            if "Buffer" in item["type"]
        ]
        add_check(
            checks,
            "skinning dispatch exposes source, output and palette buffers",
            len(storage_buffers) >= 3,
            storage_buffers,
            ">=3 storage-buffer accesses",
        )

    report["skinningDispatches"] = skinning_dispatches
    report["blasActions"] = blas_actions
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
