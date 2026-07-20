# Phase 1 item prompt template

Combine `.prompts/art/global_style_prefix.md` with:

```text
Primary request: <asset-specific object>
Input images: approved Player and default Enemy as style references; test map as palette reference
Subject: <identity details and recognizable silhouette>
Composition/framing: <upright or horizontal orientation>; keep enough empty border for the target footprint
Materials/textures: <asset-specific materials>
Constraints: preserve the requested orientation; do not include extra accessories or a second object
```

Each distinct candidate requires a separate image-generation call. Save the complete rendered prompt beside the candidate.

