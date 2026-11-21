# Frontier continuation report

## Baseline and changes

The working tree starts from the contents of `c1bbbfd` in `unassignedinbox/Frontier`.
The project-browser and timing-log changes from its descendant commits `6936e8c` and
`70fc526` were applied, followed by the camera interaction changes in this branch.
This is a source snapshot on the current Arena branch, not a checkout of the other branch.

- Plain left drag no longer moves the camera or activates WASD flight. It is reserved for
  picking and gizmo interaction; RMB + WASD/QE is fly navigation. Middle drag pans;
  Alt+left orbits and Alt+right dollies. Shift sprints; wheel changes fly speed.
- `CameraMovementLag` is a reusable exponential velocity component used for RMB flight.
  It eases acceleration and release of movement keys; leaving fly mode resets it immediately
  so a selection or gizmo drag does not drift. Mouse-look itself is not smoothed.
- The imported browser changes restrict the browser launch to Frontier, seed the default
  project, and implement new-project plane/sky behavior. Cornell-named scene assets and
  engine code were removed in that changeset. These paths still need full runtime validation.
- The RAM timing log samples selected host/editor/render CPU scopes, aggregates on a
  worker, and writes `Logs/FrontierThoughtLog.txt` on normal application shutdown. It is
  not a trace of every operation or GPU execution time; nested scope totals overlap. The
  finite ring can drop records if producers overrun it, and abnormal termination may lose
  the in-memory data. Its report is a summary, not a per-event timeline.

## UI inventory

See `Documentation/UIControllableItems.md` for the available working inventory grouped by
viewport, outliner, camera, sun, moons, stars, atmosphere, clouds, and other components.
The imported list distinguishes UI entries from backend configuration. It has not been fully
verified for every renderer or control path and must not be treated as a complete guarantee;
individual backend fields may not affect the active viewport until wired. It is a starting point
for the prototype, not proof of exhaustive coverage.

## SDF AO and XeGTAO assessment (report only)

- The GPU GDF AO in `WorldViewportRadiometricCompute.slang` takes four samples along the
  shading normal, starting with a bias based on the fine voxel size. It estimates occlusion
  from distance deficits. Coarse voxels, thin features, normal mismatch, surface bias and
  just four samples can cause self-occlusion, halos and unstable detail. These are plausible
  causes, not a diagnosis of the screenshot: no field visualisation or GPU capture was run.
- The AO debug channel shows the resulting scalar; it is not a visualisation of the SDF
  surface or material IDs. The requested SDF surface, material and other diagnostic views
  are not implemented in this turn. A useful next diagnostic set would show zero-isosurface,
  signed distance/clipmap level, voxel size and surface normals, plus nearest-source material
  IDs only if that data is actually stored. Validate against a reference scene and camera.
- XeGTAO is a screen-space AO technique; it can provide attractive local PBR contact shading
  without GI and is worth evaluating as a separately selectable AO mode. It cannot see
  off-screen or hidden geometry and is not a drop-in replacement for world-space SDF data.
  Integration requires depth and normals, denoising and temporal handling, quality/performance
  controls, correct compositing, and GPU testing. Do not multiply two AO methods blindly.
  Compare XeGTAO and corrected SDF AO at matched scenes/resolutions before choosing a default.
- No AO algorithm or SDF debug views were changed, and XeGTAO was not implemented; the
  request specified a report only for this part.

## Validation and limits

The reusable lag component was compiled and exercised at 30/60 Hz; the timing log was
compiled standalone and tested with 40,000 samples from four threads, checking no report
existed before shutdown and that all samples appeared afterward. No full application build,
interactive gizmo test, screenshot comparison or GPU verification was possible in this
sandbox: CMake, Ninja, the shader toolchain and project dependencies are not installed.
