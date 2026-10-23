## graphify

This project has a knowledge graph at graphify-out/ with god nodes, community structure, and cross-file relationships.

When the user types `/graphify`, use the installed graphify skill or instructions before doing anything else.

Rules:
- When graphify-out/graph.json exists, run `graphify query "<question>"` before your first grep, search or source-file read for a codebase question, and put this rule in every subagent prompt that explores code. Read raw files only for lines the graph or an earlier read already located. Use `graphify path "<A>" "<B>"` for relationships and `graphify explain "<concept>"` for focused concepts. These return a scoped subgraph, usually much smaller than GRAPH_REPORT.md or raw grep output.
- Dirty graphify-out/ files are expected after hooks or incremental updates; dirty graph files are not a reason to skip graphify. Only skip graphify if the task is about stale or incorrect graph output, or the user explicitly says not to use it.
- If graphify-out/wiki/index.md exists, use it for broad navigation instead of raw source browsing.
- Read graphify-out/GRAPH_REPORT.md only for broad architecture review or when query/path/explain do not surface enough context.
- After modifying code, run `graphify update .` to keep the graph current (AST-only, no API cost).
- A committed graphify-out/graph.json is rewritten whole on every build, so a large diff to it in a pull request is expected. Do not flag it as churn or suggest skipping the graph rebuild or its hooks.

## Obsidian

The repository's local Obsidian vault is at `graphify-out/obsidian/`. Read and edit its Markdown files directly; no running Obsidian app or connector is required.

- For codebase questions, query graphify first, then consult the relevant vault notes and follow their wiki links for supporting context.
- Treat exported notes as generated views of the graph. Verify inferred relationships against source before making changes.
- After updating the graph, run `.codex/scripts/refresh_dependency_graph.py` using the Python interpreter recorded in `graphify-out/.graphify_python`. It reattaches configured dependency graphs and refreshes the vault with Windows-safe filenames.
- Keep handwritten notes separate from generated notes so exports do not overwrite them.

## Dependency graphs

`.codex/graphify-dependencies.json` includes the local Vulkanizer graph as a source reference. After `graphify update .`, run `.codex/scripts/refresh_dependency_graph.py` with the recorded graphify interpreter. If Vulkanizer code changed, update its own graph first. Consumer package pins remain unchanged; the dependency nodes identify the local source version separately.
