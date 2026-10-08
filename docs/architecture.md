# Architecture and acceptance

## Data path and ownership

`Host Streamer / external sender → bounded RX → line validator → motion queue → timed execution → immutable snapshot → OpenGL scene graph`.

Realtime bytes branch before RX. Line `ok` is emitted only after a valid block has been accepted or a synchronous operation completed; motion completion has its own counter and does not emit a second `ok`. No UI action directly moves mesh transforms. A single GUI thread owns the controller and WinSerial; OVERLAPPED poll does not block on read/write completion. This is an approximate desktop simulator, not a hard realtime execution environment.

The internal sender owns the response credit ledger. Incoming `ok/error` removes one outstanding line's exact byte count. Alarm stops subsequent sending but leaves pending responses. Reset/banner clears the ledger. COM mode grants ordinary line-response ownership to the external sender. Local realtime controls do not create line acknowledgements.

## Machine scene graph

```text
frame
 ├─ Y-table transform: world Z = 90 - machine Y
 │   ├─ table and workholding
 │   ├─ workpiece
 │   └─ executed trail in table-local coordinates
 └─ fixed gantry
     └─ X-carriage transform: world X = machine X - 150
         └─ Z-carriage transform: world Y = machine Z + configured tool datum (default demo 154, automatic imports 148)
             └─ spindle, collet and cutting tool
```

Qt OpenGL 3.3 core shader, VAO/VBO triangle meshes, perspective and depth test. Mouse camera affects view matrices only. Meshes are rebuilt when a new context initializes; old vertex ranges are cleared. Geometry follows the independent Lunyee profile; table holes and spindle clamp bore are open triangle meshes, etched lines are single faces with polygon offset. Trails record executed cutting passes with spindle on, below the wood surface, and have explicit segment break markers when cutting ends, to avoid fabricated Jog/Home lines.

## Acceptance already exercised

- CMake Release build with warnings as errors; 8 CTest suites, including closed-port transport lifecycle validation.
- Packaged executable launches without MSYS2/Qt on PATH.
- QOpenGLWidget initializes on Intel UHD Graphics 730, screenshot saved.
- Internal demo: 17 lines sent = confirmed = total, 12 motion blocks completed, Idle, RX 0, planner 0, no stream errors. Wood top = world height 84 = machine Z -70; working Z -1/-2 lies below the surface, clearance Z5 above it.

## Remaining acceptance work

- Install/configure a virtual COM port pair and test an actual sender: connection, startup/reset, streaming, pause/resume, Jog cancel, reconnection.
- Compare the implemented subset's transcript with the user's specified GRBL firmware/configuration.
- Replace healthy-switch homing with modeled switch faults if requested; implement a real look-ahead planner before claiming firmware-equivalent motion.
- Test portable ZIP on a second clean Windows machine and other graphics drivers.


## v0.3 coordinate acceptance

User-selected top-left usable-area XY0: X right, Y forward/screen down, Z up. Eight healthy-switch phases physically locate Z positive and XY negative switches, then park at machine (0,0,-2). XY switch contacts lie at -2 in this chosen frame; only Home may seek outside normal XY bounds. Snapshot switch/reference fields, positive/negative Jog, G92 separation, mirrored G2/G3 and actual machine/table transforms are tested. See coordinates-v0.3.md. GUI smoke records actual Home and Jog frames before running the full 17-line demo.



## v0.4 material removal

Controller executes motion and synchronously publishes MotionStep to Stock. The stock does not read host credit, acknowledgement counts, or GUI snapshots to decide cuts. Stock owns a bounded height field and tool profile, computes analytical minima over linear sweeps, and handles arc substeps from the existing 2ms integrator. Same-direction collinear linear steps may merge to reduce work; arc steps are never merged. Synchronize standalone S words after queued motion so later spindle speeds cannot apply early to earlier cuts.

StockSetup maps physical machine XY and tool-tip height to fixed material samples. G92 changes only controller coordinates. Stock persists across controller reset, hold, tool change, and repeated programs. Explicit restore/configure establishes new material. GUI prepares WCO for imported jobs at idle, with no movement; reset invalidates WCO and marks automatic setup for preparation after Home.

MachineView draws indexed 96-cell tiles with surface normals, bottom/side closure, and updates affected tiles. A height texture supplies approximate local cavity shadows; procedural wood material supplies color variation. Meshes are regenerated from current heights after OpenGL context recreation. STL uses those same geometric tiles, no visual-only displacement.

Eleven CTest suites cover prior machine contracts plus material geometry and import contracts. Real Desktop files are fully streamed through Controller/Streamer in --carve-test, with statistics, source hashes, opaque close-ups and optional STL export. --repeat proves idempotence; --continue proves same-stock/tool-change continuation; --grid measures finer material sampling. Detailed local validation reports and private input files are not included in the source repository.

## v0.5 fixtures and camera

FixtureSetup owns up to four independent table-local press clamps. Automatic placement derives a pair from StockSetup; manual positions survive stock import/movement. WorkspaceSetup serializes stock, tool, fixture and import choices to validated JSON. Changes to fixture rendering rebuild static scene geometry while preserving Stock heights and controller work offsets. UI rechecks idle after modal dialogs before applying changes. Portable setup.json restores configuration at normal startup; carve/smoke test modes isolate user preferences.

Right-drag pans the camera target using the current view right/up basis and projection world units per logical pixel. It never mutates controller position or stock coordinates. Preset/reset views clear pan, and stock close-up presets choose distance from stock dimensions. Perspective/rotated/orthographic event tests verify 120x50 mouse displacement maps to 120x50 screen displacement without material/controller changes.
