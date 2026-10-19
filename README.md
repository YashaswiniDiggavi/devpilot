# DevPilot

A C++20 agent that fixes broken CMake builds. It builds a project in a Docker
sandbox, parses the compiler errors, asks an LLM for a minimal patch, validates
and applies it, then rebuilds until the build passes.

## How it works
build (sandboxed) -> parse diagnostics -> LLM proposes JSON edits ->
validate + apply patch -> rebuild -> repeat (max 5 iterations)

## Safety
- Builds run in Docker: no network, 1 GB memory, 2 CPUs, PID limit,
  all capabilities dropped, no-new-privileges, non-root user.
- The model returns small search/replace edits, never whole files or commands.
- Each edit must stay inside the repo, avoid .git/.github/build, match exactly
  once, and stay under a size cap. Failed patches are reverted.
- An iteration budget stops runaway loops.

## Reliability
- Process runner with timeouts, process-group kill, and an output cap.
- LLM client with retries, exponential backoff, and daily-quota detection.
- On-disk response cache makes benchmark reruns free and reproducible.

## Results
Benchmark of 8 seeded build failures (undeclared identifier, linker error,
missing include, missing semicolon, type mismatch, wrong argument count,
const violation, multiple errors), run with builds sandboxed in Docker:
fixed 8/8, 6/8 on the first attempt, ~370 input / ~120 output tokens per bug.
Run it with `./scripts/eval.sh`.

## Build and run
```bash
cmake -B build -G Ninja && cmake --build build
docker build -t devpilot-sandbox -f docker/sandbox.Dockerfile docker
export GEMINI_API_KEY=...
./build/devpilot path/to/broken/project   # run on a copy: it edits files in place
```

## Limitations
- Only fixes build failures so far (not failing tests).
- Benchmark bugs are small; harder multi-file bugs are next.
- Edits are applied on the host; only builds are sandboxed.
