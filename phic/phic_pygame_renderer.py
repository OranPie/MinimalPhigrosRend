"""Compatibility entrypoint.

The original project shipped as a single file: phic_pygame_renderer.py.
This thin wrapper keeps the same name while delegating to the multi-module package.
"""

from phic_renderer.app import main

if __name__ == "__main__":
    main()
