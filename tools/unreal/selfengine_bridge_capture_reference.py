"""
Capture UE reference screenshots for SelfEngineBridge.json.

This runs inside Unreal Editor, opens a bridge scene, creates or reuses a camera
from the manifest camera record, attempts a high-res screenshot, and writes the
result or a concrete blocker back into the manifest. It is intentionally separate
from selfengine_bridge_export.py so mesh/metadata export remains deterministic.
"""

from __future__ import annotations

import argparse
import json
import math
import os
import re
import sys
from datetime import datetime, timezone
from typing import Any, Dict, List, Optional, Tuple

import unreal  # type: ignore


def _log(message: str) -> None:
    unreal.log("[SelfEngineBridgeCapture] " + message)


def _warn(message: str) -> None:
    unreal.log_warning("[SelfEngineBridgeCapture] " + message)


def _error(message: str) -> None:
    unreal.log_error("[SelfEngineBridgeCapture] " + message)


def _load_manifest(path: str) -> Dict[str, Any]:
    with open(path, "r", encoding="utf-8-sig") as file:
        return json.load(file)


def _write_manifest(path: str, manifest: Dict[str, Any]) -> None:
    with open(path, "w", encoding="utf-8") as file:
        json.dump(manifest, file, ensure_ascii=False, indent=2)
        file.write("\n")


def _safe_file_stem(value: str) -> str:
    cleaned = re.sub(r"[^A-Za-z0-9._-]+", "_", value.strip("/").replace("/", "_"))
    return cleaned.strip("._-") or "reference_capture"


def _utc_now() -> str:
    return datetime.now(timezone.utc).isoformat()


def _scene_map_path(scene: Dict[str, Any]) -> str:
    source_asset_id = str(scene.get("sourceAssetId") or scene.get("id") or "")
    if source_asset_id.startswith("/Game/"):
        return source_asset_id

    source_map_path = str(scene.get("sourceMapPath") or "")
    if source_map_path.endswith(".umap"):
        return "/Game/" + os.path.splitext(os.path.basename(source_map_path))[0]
    return source_asset_id


def _open_map(scene: Dict[str, Any]) -> bool:
    map_path = _scene_map_path(scene)
    if not map_path:
        return False
    try:
        return bool(unreal.EditorLoadingAndSavingUtils.load_map(map_path))
    except Exception as exc:
        _warn(f"Failed to open map {map_path}: {exc}")
        return False


def _find_scene(manifest: Dict[str, Any], selector: str) -> Optional[Dict[str, Any]]:
    scenes = list(manifest.get("scenes") or [])
    if not scenes:
        return None
    if not selector:
        return scenes[0]
    for scene in scenes:
        if selector in str(scene.get("id", "")) or selector in str(scene.get("name", "")):
            return scene
    return None


def _find_camera(scene: Dict[str, Any], selector: str) -> Optional[Dict[str, Any]]:
    cameras = list(scene.get("cameras") or [])
    if not cameras:
        return None
    if not selector:
        return cameras[0]
    for camera in cameras:
        fields = (
            str(camera.get("id", "")),
            str(camera.get("actorName", "")),
            str(camera.get("componentName", "")),
            str(camera.get("cameraName", "")),
        )
        if any(selector in field for field in fields):
            return camera
    return None


def _read_vec3(record: Dict[str, Any], names: Tuple[str, ...], fallback: List[float]) -> List[float]:
    for name in names:
        value = record.get(name)
        if isinstance(value, list) and len(value) >= 3:
            return [float(value[0]), float(value[1]), float(value[2])]
    return fallback


def _camera_transform(camera: Dict[str, Any]) -> Tuple[List[float], List[float], List[float]]:
    transform = camera.get("transform") or {}
    if not isinstance(transform, dict):
        transform = {}
    position = _read_vec3(transform, ("position", "translation", "location"), [0.0, 0.0, 0.0])
    forward = _read_vec3(transform, ("forward", "direction"), [0.0, 0.0, -1.0])
    up = _read_vec3(transform, ("up",), [0.0, 0.0, 1.0])
    position = _read_vec3(camera, ("position", "translation", "location"), position)
    forward = _read_vec3(camera, ("forward", "direction"), forward)
    up = _read_vec3(camera, ("up",), up)
    return position, forward, up


def _normalize(value: List[float], fallback: List[float]) -> List[float]:
    length = math.sqrt(value[0] * value[0] + value[1] * value[1] + value[2] * value[2])
    if length <= 0.0001:
        return fallback
    return [value[0] / length, value[1] / length, value[2] / length]


def _forward_to_rotator(forward: List[float]) -> Any:
    direction = _normalize(forward, [1.0, 0.0, 0.0])
    try:
        vector = unreal.Vector(direction[0], direction[1], direction[2])
        if hasattr(vector, "to_orientation_rotator"):
            return vector.to_orientation_rotator()
        if hasattr(vector, "rotation"):
            return vector.rotation()
    except Exception:
        pass
    xy = math.sqrt(direction[0] * direction[0] + direction[1] * direction[1])
    pitch = math.degrees(math.atan2(direction[2], xy))
    yaw = math.degrees(math.atan2(direction[1], direction[0]))
    return unreal.Rotator(pitch, yaw, 0.0)


def _spawn_camera_actor(camera: Dict[str, Any]) -> Any:
    position, forward, _up = _camera_transform(camera)
    location = unreal.Vector(position[0], position[1], position[2])
    rotation = _forward_to_rotator(forward)
    try:
        actor = unreal.EditorLevelLibrary.spawn_actor_from_class(unreal.CameraActor, location, rotation)
    except Exception:
        actor = unreal.EditorActorSubsystem().spawn_actor_from_class(unreal.CameraActor, location, rotation)

    if actor is None:
        raise RuntimeError("UE failed to spawn CameraActor for reference capture")

    try:
        actor.set_actor_label(str(camera.get("actorName") or "SelfEngineReferenceCamera"))
    except Exception:
        pass

    component = getattr(actor, "camera_component", None)
    if component is None:
        component = actor.get_editor_property("camera_component")
    if component is not None:
        fov = float(camera.get("fieldOfView") or camera.get("fieldOfViewDegrees") or camera.get("fov") or 60.0)
        aspect = float(camera.get("aspectRatio") or 16.0 / 9.0)
        try:
            component.set_editor_property("field_of_view", fov)
        except Exception:
            pass
        try:
            component.set_editor_property("aspect_ratio", aspect)
        except Exception:
            pass
        try:
            component.set_editor_property("constrain_aspect_ratio", True)
        except Exception:
            pass
    return actor


def _actor_rotator_record(actor: Any) -> Dict[str, float]:
    rotation = actor.get_actor_rotation()
    return {
        "pitch": float(getattr(rotation, "pitch", 0.0)),
        "yaw": float(getattr(rotation, "yaw", 0.0)),
        "roll": float(getattr(rotation, "roll", 0.0)),
    }


def _set_editor_view(camera_actor: Any) -> None:
    location = camera_actor.get_actor_location()
    rotation = camera_actor.get_actor_rotation()
    try:
        unreal.EditorLevelLibrary.editor_set_view_location(location)
        unreal.EditorLevelLibrary.editor_set_view_rotation(rotation)
    except Exception:
        pass
    try:
        subsystem = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
        if hasattr(subsystem, "set_level_viewport_camera_info"):
            subsystem.set_level_viewport_camera_info(location, rotation)
    except Exception:
        pass


def _quit_editor_if_requested(should_quit: bool) -> None:
    if not should_quit:
        return
    try:
        unreal.SystemLibrary.quit_editor()
        return
    except Exception:
        pass
    try:
        unreal.SystemLibrary.execute_console_command(None, "QUIT_EDITOR")
    except Exception:
        pass


def _ensure_reference_capture(scene: Dict[str, Any], record: Dict[str, Any]) -> Dict[str, Any]:
    record_id = str(record.get("id") or "")
    captures = list(scene.get("referenceCaptures") or [])
    for index, capture in enumerate(captures):
        if str(capture.get("id") or "") == record_id:
            merged = dict(capture)
            merged.update(record)
            captures[index] = merged
            scene["referenceCaptures"] = captures
            return merged
    captures.append(record)
    scene["referenceCaptures"] = captures
    return record


def _append_diagnostic(manifest: Dict[str, Any], message: str, status: str) -> None:
    diagnostics = manifest.setdefault("diagnostics", {})
    entries = diagnostics.setdefault("ueReferenceCapture", [])
    entries.append(
        {
            "timeUtc": _utc_now(),
            "status": status,
            "message": message,
        }
    )


def _find_new_image(output_path: str, since: float) -> str:
    if os.path.isfile(output_path) and os.path.getmtime(output_path) >= since:
        return output_path

    output_dir = os.path.dirname(output_path)
    if not os.path.isdir(output_dir):
        return ""
    candidates: List[str] = []
    for name in os.listdir(output_dir):
        path = os.path.join(output_dir, name)
        if not os.path.isfile(path):
            continue
        if os.path.splitext(path)[1].lower() not in (".png", ".jpg", ".jpeg", ".bmp", ".exr", ".hdr"):
            continue
        if os.path.getmtime(path) >= since:
            candidates.append(path)
    candidates.sort(key=lambda item: os.path.getmtime(item), reverse=True)
    return candidates[0] if candidates else ""


def _request_capture(output_path: str, width: int, height: int, camera_actor: Any) -> Tuple[bool, Any, str, str]:
    os.makedirs(os.path.dirname(output_path), exist_ok=True)

    automation_library = getattr(unreal, "AutomationLibrary", None)
    if automation_library is None or not hasattr(automation_library, "take_high_res_screenshot"):
        return False, None, "capture_blocked_missing_automation_library", "unreal.AutomationLibrary.take_high_res_screenshot is not available"

    errors: List[str] = []
    task = None
    call_attempts = (
        lambda: automation_library.take_high_res_screenshot(width, height, output_path, camera=camera_actor),
        lambda: automation_library.take_high_res_screenshot(width, height, output_path, camera_actor),
        lambda: automation_library.take_high_res_screenshot(width, height, output_path),
    )
    for attempt in call_attempts:
        try:
            task = attempt()
            break
        except Exception as exc:
            errors.append(str(exc))

    if task is None:
        return False, None, "capture_failed_api_call", "; ".join(errors)

    return True, task, "", ""


def _default_output_path(manifest_path: str, scene: Dict[str, Any], camera: Dict[str, Any]) -> str:
    root = os.path.dirname(manifest_path)
    project_root = str(root)
    output_root = os.path.join(project_root, "Saved", "SelfEngineBridge", "ReferenceCaptures")
    stem = _safe_file_stem(str(scene.get("id") or scene.get("name") or "scene"))
    camera_stem = _safe_file_stem(str(camera.get("id") or camera.get("actorName") or "camera"))
    return os.path.abspath(os.path.join(output_root, f"{stem}_{camera_stem}.png"))


def parse_args(argv: List[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument("--manifest", default=os.environ.get("SELFENGINE_BRIDGE_MANIFEST", ""))
    parser.add_argument("--scene", default=os.environ.get("SELFENGINE_BRIDGE_SCENE", ""))
    parser.add_argument("--camera", default=os.environ.get("SELFENGINE_BRIDGE_CAMERA_ID", ""))
    parser.add_argument("--output", default=os.environ.get("SELFENGINE_REFERENCE_CAPTURE_OUTPUT", ""))
    parser.add_argument("--width", type=int, default=int(os.environ.get("SELFENGINE_REFERENCE_CAPTURE_WIDTH", "1920") or "1920"))
    parser.add_argument("--height", type=int, default=int(os.environ.get("SELFENGINE_REFERENCE_CAPTURE_HEIGHT", "1080") or "1080"))
    parser.add_argument("--wait-seconds", type=float, default=float(os.environ.get("SELFENGINE_REFERENCE_CAPTURE_WAIT_SECONDS", "8") or "8"))
    parser.add_argument("--no-quit", action="store_true")
    args = parser.parse_args(argv)
    args.no_quit = args.no_quit or (
        os.environ.get("SELFENGINE_REFERENCE_CAPTURE_NO_QUIT", "").lower() in ("1", "true", "yes", "on")
    )
    if not args.manifest:
        parser.error("the following arguments are required: --manifest or SELFENGINE_BRIDGE_MANIFEST")
    return args


def main(argv: List[str]) -> int:
    args = parse_args(argv)
    manifest_path = os.path.abspath(args.manifest)
    manifest = _load_manifest(manifest_path)
    scene = _find_scene(manifest, args.scene)
    if scene is None:
        _append_diagnostic(manifest, f"No scene matched selector '{args.scene}'", "capture_failed_scene_missing")
        _write_manifest(manifest_path, manifest)
        _quit_editor_if_requested(not args.no_quit)
        return 2

    camera = _find_camera(scene, args.camera)
    if camera is None:
        capture_id = f"{scene.get('id', 'scene')}:MissingCamera"
        _ensure_reference_capture(scene, {
            "id": capture_id,
            "sceneId": scene.get("id", ""),
            "cameraId": args.camera,
            "status": "capture_blocked_missing_camera",
            "source": "ue_editor_reference_capture",
            "attemptedAtUtc": _utc_now(),
            "screenshotPath": "",
            "message": "No camera record exists in the bridge manifest for this scene.",
        })
        scene["referenceCaptureCount"] = len(scene.get("referenceCaptures") or [])
        _append_diagnostic(manifest, "No camera record exists in the bridge manifest for this scene", "capture_blocked_missing_camera")
        _write_manifest(manifest_path, manifest)
        _quit_editor_if_requested(not args.no_quit)
        return 3

    if not _open_map(scene):
        _append_diagnostic(manifest, f"Failed to open map for scene '{scene.get('id', '')}'", "capture_failed_map_open")
        _write_manifest(manifest_path, manifest)
        _quit_editor_if_requested(not args.no_quit)
        return 4

    output_path = os.path.abspath(args.output) if args.output else _default_output_path(manifest_path, scene, camera)
    camera_actor = _spawn_camera_actor(camera)
    _set_editor_view(camera_actor)

    scene_id = str(scene.get("id") or scene.get("name") or "")
    camera_id = str(camera.get("id") or args.camera or "camera")
    capture_id = f"{scene_id}:{camera_id}:UEReference"
    base_record = {
        "id": capture_id,
        "sceneId": scene_id,
        "cameraId": camera_id,
        "cameraName": camera.get("actorName", camera.get("componentName", "")),
        "source": "ue_editor_reference_capture",
        "attemptedAtUtc": _utc_now(),
        "width": max(args.width, 1),
        "height": max(args.height, 1),
        "ueCameraActorRotation": _actor_rotator_record(camera_actor),
        "bridgeCameraForward": _camera_transform(camera)[1],
    }

    request_ok, task, status, message = _request_capture(output_path, max(args.width, 1), max(args.height, 1), camera_actor)
    if not request_ok:
        record = dict(base_record)
        record.update({
            "status": status,
            "screenshotPath": "",
            "plannedScreenshotPath": output_path,
            "message": message,
        })
        _ensure_reference_capture(scene, record)
        scene["referenceCaptureCount"] = len(scene.get("referenceCaptures") or [])
        _append_diagnostic(manifest, message, status)
        _write_manifest(manifest_path, manifest)
        _warn(message)
        _quit_editor_if_requested(not args.no_quit)
        return 5

    start_wall = datetime.now(timezone.utc)
    tick_handle = {"value": None}
    finished = {"value": False}

    def finish_success(image_path: str) -> None:
        if finished["value"]:
            return
        finished["value"] = True
        record = dict(base_record)
        record.update({
            "status": "captured",
            "screenshotPath": os.path.abspath(image_path),
            "sourceImagePath": os.path.abspath(image_path),
            "message": "UE reference screenshot captured from bridge camera.",
            "capturedAtUtc": _utc_now(),
        })
        _ensure_reference_capture(scene, record)
        scene["referenceCaptureCount"] = len(scene.get("referenceCaptures") or [])
        _append_diagnostic(manifest, f"Captured UE reference screenshot: {image_path}", "captured")
        _write_manifest(manifest_path, manifest)
        _log(f"Captured UE reference screenshot: {image_path}")
        if tick_handle["value"] is not None:
            unreal.unregister_slate_post_tick_callback(tick_handle["value"])
        _quit_editor_if_requested(not args.no_quit)

    def finish_failure(failure_status: str, failure_message: str) -> None:
        if finished["value"]:
            return
        finished["value"] = True
        image_path = _find_new_image(output_path, start_wall.timestamp())
        if image_path:
            finish_success(image_path)
            return
        record = dict(base_record)
        record.update({
            "status": failure_status,
            "screenshotPath": "",
            "plannedScreenshotPath": output_path,
            "message": failure_message,
            "failedAtUtc": _utc_now(),
        })
        _ensure_reference_capture(scene, record)
        scene["referenceCaptureCount"] = len(scene.get("referenceCaptures") or [])
        _append_diagnostic(manifest, failure_message, failure_status)
        _write_manifest(manifest_path, manifest)
        _warn(failure_message)
        if tick_handle["value"] is not None:
            unreal.unregister_slate_post_tick_callback(tick_handle["value"])
        _quit_editor_if_requested(not args.no_quit)

    def on_tick(delta_seconds: float) -> bool:
        del delta_seconds
        if finished["value"]:
            return False
        image_path = _find_new_image(output_path, start_wall.timestamp())
        if image_path:
            finish_success(image_path)
            return False
        elapsed = (datetime.now(timezone.utc) - start_wall).total_seconds()
        task_done = False
        try:
            task_done = hasattr(task, "is_task_done") and bool(task.is_task_done())
        except Exception:
            task_done = False
        if elapsed >= max(args.wait_seconds, 0.5):
            finish_failure(
                "capture_blocked_no_rendered_image",
                "UE accepted the screenshot request but no image was written before timeout. "
                "This usually means the editor session has no renderable viewport or the screenshot task did not tick."
            )
            return False
        return True

    tick_handle["value"] = unreal.register_slate_post_tick_callback(on_tick)
    _log(f"UE reference screenshot requested: {output_path}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
