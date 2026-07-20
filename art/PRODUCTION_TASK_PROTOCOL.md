# Art Production Task Protocol

## Art-control task

The art-control task is an orchestrator and reviewer, not an image generator.
For every asset package it must:

1. Read the project art bible, manifest, current runtime contract, and approved
   anchors.
2. Define a bounded package ID, output inventory, candidate count, style
   anchors, technical constraints, negative constraints, and acceptance gate.
3. Write the exact production brief and prompts under `.prompts/art/`.
4. Create a dedicated user-visible Codex task for that asset family and send it
   the brief.
5. Wait for `result.json` and candidate paths from the production task.
6. Review visual identity, style consistency, dimensions, Alpha, pivots,
   margins, naming, and runtime fit.
7. Send precise repair instructions back to the same production task when
   correction is required.
8. Select and publish only approved candidates, update the manifest and QA
   reports, then archive the completed package.
9. Start the next production task only after dependencies and acceptance gates
   permit it.

## Production task

A production task owns exactly one coherent family or package. It may:

- execute the supplied image-generation calls;
- save exact prompts, raw renders, transparent candidates, and local QA;
- recommend candidates and explain risks;
- respond to repair prompts from the art-control task.

It may not:

- modify `art/ASSET_MANIFEST.yaml`;
- publish into `assets/`;
- approve its own candidates;
- redesign other asset families;
- start downstream production tasks.

## Handoff contract

Every production task returns a `result.json` containing:

```json
{
  "package_id": "map_roads_pack_v1",
  "status": "generated_pending_control_review",
  "calls_completed": 0,
  "assets": [],
  "recommended_candidates": {},
  "known_risks": [],
  "written_root": "art/work/map_roads_pack_v1/"
}
```

Each asset entry records its asset ID, prompt path, raw render path,
transparent-candidate path, dimensions, Alpha checks, and production-task
recommendation. The art-control task treats recommendations as evidence, not
approval.
