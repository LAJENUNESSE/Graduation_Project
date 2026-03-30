# Vulkan Migration Plan (Ultrawork + TDD)

## 1) Intent, Scope, and Non-Goals

### Intent
Migrate the engine from OpenGL 4.3 to a production-capable Vulkan backend while preserving CUDA-accelerated particle/SPH paths, replacing CUDA-OpenGL interop with CUDA-Vulkan external memory + timeline semaphore synchronization.

### Scope
- Introduce Vulkan as a first-class renderer backend behind existing renderer abstractions.
- Reuse existing CUDA-Vulkan interop assets in `Engine/Platform/CUDA/` instead of re-implementing them.
- Keep OpenGL backend buildable during migration (dual-backend period).
- Deliver migration in verifiable increments with TDD-oriented gates.

### Non-Goals (for this migration wave)
- No full render-graph rewrite.
- No ECS/scene architecture redesign unrelated to backend migration.
- No mandatory immediate deletion of OpenGL backend until Vulkan parity gates pass.

---

## 2) Current Baseline and Reuse-First Strategy

### Known baseline pain
- CUDA-GL interop introduces implicit sync, causing `SwapBuffers` stalls and frame-time spikes.

### Reuse-first assets (must be leveraged)
- `Engine/Platform/CUDA/VulkanInteropCommon.h`
  - Use `BuildInteropFrameSyncValues()` as the canonical frame-sync value policy.
- `Engine/Platform/CUDA/VulkanExternalInterop.h/.cpp`
  - Reuse `CudaExternalInteropContext` for CUDA import of Vulkan-exported memory/semaphores.
  - Reuse `VulkanExternalExport` helpers for handle type policy.
- `Engine/Platform/CUDA/VulkanExternalRuntime.cpp`
  - Extract/adapt proven patterns for Vulkan instance/device/queue creation and exportable allocations.
- `Engine/Platform/CUDA/VulkanExternalSmokeKernel.cu/.h`
  - Keep as a smoke/interop verification fixture.

### Replace/remove targets
- `CudaGLInteropContext` (all usage paths)
- GL registration/map/unmap synchronization in:
  - `ParticleSystemGPU`
  - `FluidSystemGPU`

---

## 3) Architecture Decisions (for execution)

1. **Strangler migration, not big-bang**
   - Keep OpenGL functioning while Vulkan slices are landed.
   - Introduce Vulkan implementations behind existing interfaces first.

2. **Abstraction-first**
   - Enforce backend selection through `RendererAPI` and `GraphicsContext` factories.
   - Remove direct GL calls from higher-level renderer code gradually.

3. **Interop-first for CUDA-critical paths**
   - Treat CUDA-Vulkan synchronization correctness as a hard gate.
   - No performance claims before synchronization correctness tests pass.

4. **TDD execution contract**
   - Every migration slice defines:
     - RED: failing test/check that demonstrates missing behavior.
     - GREEN: minimal implementation to pass.
     - REFACTOR: cleanup while preserving green.
   - No feature slice is “done” without passing automated or scripted checks.

---

## 4) Ultrawork Execution Model

### 4.1 Workstream decomposition
- **WS-A: Platform & Device Core**
  Vulkan instance/device/queue/swapchain/context.
- **WS-B: Resource Layer**
  Buffers/textures/shaders/framebuffers/descriptors/pipelines.
- **WS-C: Renderer Integration**
  Scene renderer, ImGui backend path, debug draw and state plumbing.
- **WS-D: CUDA Interop Migration**
  Particle/SPH path migration to Vulkan external memory/timeline semaphores.
- **WS-E: Verification & Perf**
  Test harnesses, smoke scenes, perf counters, regression gates.

### 4.2 Dependency ordering
1. WS-A foundation
2. WS-B minimal renderable path
3. WS-C baseline frame output
4. WS-D CUDA interop switch-over
5. WS-E parity and performance hardening

### 4.3 Ultrawork checkpoints
At each checkpoint, require all of:
- Build passes for affected targets (`Engine`, `Editor`).
- Defined RED/GREEN checks pass.
- No new diagnostics in touched files.
- Rollback path documented (feature flag/backend switch).

---

## 5) TDD-Oriented Migration Phases

## Phase 0 — Preflight and Safety Rails

### Objective
Establish migration-safe toggles, diagnostics visibility, and reproducible verification commands.

### RED
- Add/enable checks that fail when Vulkan backend enum/factory branch is missing.
- Add scripted check failing if direct GL calls appear in designated abstraction-owned files.

### GREEN
- Add Vulkan enum path in renderer API selection and factory scaffolding.
- Add build-time option and runtime selection guardrails (while OpenGL remains default if needed).

### REFACTOR
- Consolidate backend selection logic into single source of truth.

### Exit criteria
- Can compile with Vulkan backend code paths included.
- OpenGL path remains functional.

---

## Phase 1 — Vulkan Core Bring-up (WS-A)

### Objective
Create minimal Vulkan context that can initialize device/surface/swapchain and execute present loop skeleton.

### Planned files
- `Engine/Platform/Vulkan/VulkanCommon.h`
- `Engine/Platform/Vulkan/VulkanDevice.h/.cpp`
- `Engine/Platform/Vulkan/VulkanContext.h/.cpp`
- `Engine/Platform/Vulkan/VulkanSwapchain.h/.cpp`
- Build system integration (`Engine/CMakeLists.txt`, related root CMake files)

### RED
- Smoke executable/test that attempts Vulkan init and fails before implementation.

### GREEN
- Vulkan instance/device/queue/surface/swapchain creation succeeds.
- Acquire/present loop path works in a minimal frame skeleton.

### REFACTOR
- Factor reusable extension/layer checks and queue-family selection helpers.

### Exit criteria
- Deterministic startup and clean shutdown without validation errors in baseline scenario.

---

## Phase 2 — Resource and Shader Pipeline Foundation (WS-B)

### Objective
Enable minimal draw/compute-capable resource stack under Vulkan.

### Planned files
- `Engine/Platform/Vulkan/VulkanMemory.h/.cpp`
- `Engine/Platform/Vulkan/VulkanBuffer.h/.cpp`
- `Engine/Platform/Vulkan/VulkanTexture.h/.cpp`
- `Engine/Platform/Vulkan/VulkanShader.h/.cpp`
- `Engine/Platform/Vulkan/VulkanDescriptor.h/.cpp`
- `Engine/Platform/Vulkan/VulkanPipeline.h/.cpp`
- `Engine/Platform/Vulkan/VulkanFramebuffer.h/.cpp`
- `Engine/Platform/Vulkan/VulkanVertexArray.h/.cpp` (or equivalent binding/pipeline adapter)

### RED
- Tests/checks for:
  - Buffer creation + upload lifecycle
  - Shader compilation path from existing GLSL assets
  - Descriptor binding mismatch detection

### GREEN
- Vertex/index/storage buffer operations work.
- GLSL to SPIR-V flow works for target shader subset.
- Basic framebuffer/pipeline path can render a minimal pass.

### REFACTOR
- Introduce cache layers (pipeline/shader module) only after baseline correctness.

### Exit criteria
- Minimal scene primitives render with Vulkan backend.

---

## Phase 3 — Renderer Integration and API De-GL-ification (WS-C)

### Objective
Make high-level renderer code backend-agnostic by removing direct GL assumptions.

### Target areas
- `Engine/src/Renderer/RendererAPI*`
- `Engine/src/Renderer/SceneRenderer.cpp`
- `Engine/src/ImGui/ImGuiLayer*`
- `Engine/src/Physics/PhysicsDebugDraw.cpp`

### RED
- Static check fails on direct `gl*` calls in designated high-level files.
- Integration smoke fails when Vulkan backend selected.

### GREEN
- SceneRenderer path works without direct GL calls.
- ImGui path functions under Vulkan backend.
- Debug draw path runs through abstraction.

### REFACTOR
- Remove duplicated state management logic and centralize backend state transitions.

### Exit criteria
- Editor frame composition runs end-to-end on Vulkan for non-CUDA scene path.

---

## Phase 4 — CUDA Interop Migration (WS-D)

### Objective
Replace CUDA-GL interop with CUDA-Vulkan external memory + timeline semaphore flow.

### Planned files
- New: `Engine/Platform/CUDA/CudaVulkanInteropContext.h/.cpp`
- Migration updates:
  - `Engine/src/Renderer/ParticleSystemGPU.h/.cpp`
  - `Engine/src/Renderer/FluidSystemGPU.h/.cpp`

### RED
- Interop smoke test fails when external memory/timeline path is absent or misordered.
- Synchronization contract check fails when expected timeline values are not observed.

### GREEN
- CUDA imports Vulkan-exported resources and executes kernels correctly.
- Timeline wait/signal ordering matches `BuildInteropFrameSyncValues()` policy.
- Particle + SPH functional path works without GL map/unmap APIs.

### REFACTOR
- Deduplicate interop orchestration between particle and SPH paths.

### Exit criteria
- CUDA-enabled runs complete with stable synchronization and no legacy CUDA-GL calls.

---

## Phase 5 — Compatibility, Performance, and Hardening (WS-E)

### Objective
Close parity gaps and verify migration goals quantitatively.

### RED
- Baseline performance guard fails against defined targets.
- Regression tests fail on Vulkan-specific edge cases (resize, descriptor churn, shader reload).

### GREEN
- Swap/present latency and frame-time stability meet targets in representative scenes.
- Feature parity checklist passes for required rendering and simulation scenarios.

### REFACTOR
- Optimize only after correctness/parity are stable.

### Exit criteria
- Vulkan backend is viable as default for target platform profile.

---

## 6) Test Strategy (TDD Matrix)

## 6.1 Test layers
1. **Static/Structural checks**
   - Backend selection wiring
   - Forbidden direct GL call checks in high-level modules
2. **Initialization smoke tests**
   - Vulkan instance/device/swapchain startup and teardown
3. **Resource correctness tests**
   - Buffer/texture/shader/descriptor lifecycle
4. **Integration smoke scenes**
   - Editor startup, minimal render scene, resize/recreate path
5. **CUDA interop contract tests**
   - External memory import/export validity
   - Timeline semaphore ordering checks
6. **Performance regression checks**
   - Present latency and frame-time percentile thresholds

## 6.2 Red-Green-Refactor cadence per slice
- RED: introduce/enable failing check for a single behavior.
- GREEN: implement minimum code to pass.
- REFACTOR: improve structure, keep checks green.
- Repeat in small slices (1 behavior per commit when possible).

## 6.3 Example acceptance metrics
- Vulkan init smoke: pass on clean startup/shutdown.
- Scene render smoke: visible frame output with no critical validation errors.
- CUDA interop: deterministic timeline progression across consecutive frames.
- Perf: no periodic `SwapBuffers`-class stall pattern under CUDA-enabled run.

---

## 7) Atomic Commit Strategy (explicit)

### Principles
- One behavioral change per commit.
- Each commit is buildable.
- Prefer commits that include both code and the check that validates it.
- Avoid mixed concerns (e.g., shader toolchain + interop sync in one commit).

### Commit message convention
- Prefix: `[vulkan]`
- Template: `[vulkan] <scope>: <behavioral outcome>`

### Recommended commit sequence (example)
1. `[vulkan] build: add backend toggle and Vulkan dependency plumbing`
2. `[vulkan] api: wire Vulkan enum and factory scaffolding`
3. `[vulkan] core: implement Vulkan instance/device bootstrap`
4. `[vulkan] core: add swapchain acquire/present skeleton`
5. `[vulkan] resource: add Vulkan buffer lifecycle (vertex/index/storage)`
6. `[vulkan] shader: enable GLSL-to-SPIR-V path for baseline shaders`
7. `[vulkan] renderer: remove direct GL calls from SceneRenderer critical path`
8. `[vulkan] imgui: enable Vulkan backend integration`
9. `[vulkan] cuda: add CudaVulkanInteropContext and timeline sync`
10. `[vulkan] particle: migrate particle CUDA interop to Vulkan`
11. `[vulkan] fluid: migrate SPH CUDA interop to Vulkan`
12. `[vulkan] verify: add migration smoke/perf regression gates`

### Commit gate checklist (must pass before commit)
- Affected build target compiles.
- Defined RED/GREEN test for the slice is green.
- No net-new diagnostics in touched files.
- Rollback is possible via backend toggle or isolated revert.

---

## 8) Risk Register and Mitigations

1. **Shader compatibility drift**
   - Mitigation: staged shader subset onboarding + compile diagnostics per shader family.

2. **Synchronization bugs (CUDA-Vulkan timeline misuse)**
   - Mitigation: single canonical sync value policy + explicit interop contract tests + timeline logging hooks.

3. **Swapchain lifecycle instability (resize/minimize/resume)**
   - Mitigation: dedicated recreation tests and strict resource ownership boundaries.

4. **Validation noise obscuring true errors**
   - Mitigation: baseline validation log cleanliness milestone before scaling feature scope.

5. **Migration velocity collapse due to oversized slices**
   - Mitigation: enforce atomic commit sizing and strict phase exit criteria.

---

## 9) Rollout and Rollback Strategy

### Rollout
- Keep OpenGL and Vulkan selectable during migration.
- Enable Vulkan progressively by feature flags/checkpoints.
- Promote Vulkan to default only after parity + perf gates pass.

### Rollback
- If a slice regresses critical functionality, revert the atomic commit for that slice.
- Maintain backend toggle fallback to OpenGL for unblock.
- Never stack unverified migration commits.

---

## 10) Definition of Done (migration wave)

Migration is complete for this wave only when all are true:
- Vulkan backend runs Editor and key render paths stably.
- Particle/SPH CUDA paths use Vulkan interop (no CUDA-GL dependency in active path).
- TDD gates for core/resource/integration/interop/perf all pass.
- OpenGL fallback remains available until explicit deprecation decision.
- Documentation for build/run/test of Vulkan path is updated and reproducible.

---

## 11) Operator Notes for Ultrawork Execution

- Execute one phase as multiple micro-slices, not one large branch dump.
- Start each day/session by selecting one RED check and one tiny GREEN objective.
- End each day/session with:
  - passing checks,
  - one atomic commit (or a short series of atomic commits),
  - a clear next RED target.

This keeps migration momentum high, limits regression blast radius, and makes failures diagnosable.
