# Project Rules — CppDesk

## Important Notice
-**Rule**: NEVER include "Phase" or "phase" or "Phases" or "phases" (and anything in between) into the project's codebase.

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
