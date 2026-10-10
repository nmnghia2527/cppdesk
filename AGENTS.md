# Project Rules — CppDesk

## Important Notice
- **Rule**: NEVER include "Phase" or "phase" or "Phases" or "phases" (and anything in between) into the project's codebase.
- **Resumption**: When `/gsd-resume-work` is used or when the user asks to **continue** the current work/workflow. You MUST review the codebase/project/current-task for any incompletions or gaps between prompts or sessions.
- **Codebase Exploration & Quota Protection with Graphify:** NEVER perform manual multi-file scans, bulk reads, or recursive file crawling across a codebase. Uncontrolled reading exhausts token limits and quota. Strictly enforce this workflow:
  - **Graph First:** Check if `graphify-out/graph.json` exists in the project. If missing, run `/graphify .` first. AST extraction runs locally without burning LLM generation tokens.
  - **Pinpoint Querying:** Query the graph (`graphify query "<question>"`, `graphify path`, or Graphify MCP tools like `query_graph`, `shortest_path`, `god_nodes`) to isolate the exact 1-3 files and symbols involved. Never guess or scan directory trees sequentially.
  - **Surgical Reads Only:** Direct file reading via `view_file` is permitted ONLY for the specific target files identified by the graph, using bounded line ranges (`StartLine` and `EndLine`) immediately before editing or verifying.
  - **No Unbounded Grepping:** Never run broad recursive greps across whole repositories to orient or explore architecture. Reserve grep strictly for exact string literals, error strings, or non-code configs (`.env`, YAML, JSON) after isolating the target area.

## Phase Execution & Review Workflow
- **Pre-Phase Research:** Before researching, planning, or executing any phase, you MUST search for information by invoking `/agent-reach`. There is no exception.
- **Execution:** You MUST follow this workflow: use `gsd-discuss-phase` first, then `gsd-plan-phase`, then `gsd-execute-phase`. You can only skip `gsd-discuss-phase` if the user explicitly needs full automation, otherwise do not skip. You MUST invoke subagents that is associated with `gsd` to execute tasks accordingly.
- **Frontend & UI Design:** For any redesign or modification in the frontend / UI, you MUST invoke `/ui-ux-pro-max`,`/impeccable`,`/taste-skill` to design or redesign the components and layouts. There is no exception.
- **Post-Execution Review Triad:** After executing each phase, you MUST review the phase with `/code-review`, `/caveman-review`, and `/ponytail-review`. You MUST invoke all 3 skills for reviewing every time after finishing each phase.
- **Automated Fixes via Subagent:** After running the three review skills, you MUST invoke the `gsd-code-fixer` subagent to fix the code based on the review findings.

## Git & Repository Operations
- Never automatically add, push, or commit to GitHub unless the user explicitly requests you to.

## Terminal Execution
- ABSOLUTELY EVERY shell command for testing, linting, building, and git must be routed through the RTK wrapper (`rtk run <command>`). Never execute these tools directly.
