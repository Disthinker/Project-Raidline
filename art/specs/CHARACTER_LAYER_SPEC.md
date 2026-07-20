# Week 17 Character Layer Calibration Contract

Phase 1 freezes this geometry but does not generate clothing or body layers.

## Geometry

- source frame: `256x320`
- runtime draw size: `64x80`
- animation sheet: `1536x640`
- layout: six columns, left-facing row then right-facing row
- source foot baseline: `y = 300`
- source pivot: `(128, 300)`
- runtime pivot: `(32, 75)`

The current approved Player frames all end at source baseline `y = 300`; new layers must use the same baseline and frame order.

## Layer order

1. body
2. pants
3. footwear
4. shirt
5. armor
6. backpack
7. hair
8. headwear
9. weapon_back
10. arms_hands
11. weapon_front

Every layer uses the complete `256x320` frame canvas, including transparent space. Do not crop individual clothing layers independently.

## Weapon socket placeholder

Weapon sockets use source-frame coordinates and are recorded per animation frame and facing direction:

```text
socket_id
animation_id
frame_index
facing
x
y
z_layer
```

The schema is frozen in Phase 1. Exact hand coordinates remain unset until the Week 17 layered-character pilot.

