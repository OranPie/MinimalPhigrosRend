# phic_renderer (multi-module)

This is a multi-file split of the original `phic_pygame_renderer.py`.

## Run

From this folder:

- Package mode:
  - `python -m phic_renderer --input <chart.json|pack.zip|pack_folder> --respack <respack.zip>`

- Compatibility script:
  - `python phic_pygame_renderer.py --input ...`

Everything else (CLI args, behavior) should match the original script.

## Notes

- `phic_renderer/state.py` stores shared globals (`respack`, `expand_factor`) so that drawing/kinematics code can stay close
  to the original implementation while living in separate modules.
