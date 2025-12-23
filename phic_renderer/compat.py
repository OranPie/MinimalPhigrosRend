"""Compatibility layer for migrating to new architecture.

This module provides helpers to convert from the old args-based and global state
patterns to the new RenderConfig/ResourceContext pattern, enabling gradual migration.
"""

from __future__ import annotations
from typing import Any, Dict, Optional
import logging

from .config.schema import RenderConfig
from .core.context import ResourceContext
from . import state


logger = logging.getLogger(__name__)


def args_to_render_config(args: Any, **extra: Any) -> RenderConfig:
    """Convert CLI args (or config dict) to RenderConfig.

    This compatibility helper extracts rendering configuration from args
    and creates an immutable RenderConfig object.

    Args:
        args: Argument namespace or dict from argparse
        **extra: Additional configuration overrides

    Returns:
        RenderConfig instance
    """

    def get(key: str, default: Any = None) -> Any:
        """Get value from args or extra, with fallback."""
        if key in extra:
            return extra[key]
        if hasattr(args, key):
            return getattr(args, key)
        if isinstance(args, dict) and key in args:
            return args[key]
        return default

    # Extract configuration values
    config = RenderConfig(
        expand_factor=get("expand", 1.0),
        note_scale_x=get("note_scale_x", 1.0),
        note_scale_y=get("note_scale_y", 1.0),
        note_flow_speed_multiplier=get("note_flow_speed_multiplier", 1.0),
        note_speed_mul_affects_travel=get("note_speed_mul_affects_travel", False),
        force_line_alpha01=get("force_line_alpha01", None),
        force_line_alpha01_by_lid=get("force_line_alpha01_by_lid", None),
        render_overrender=get("overrender", None),
        trail_alpha=get("trail_alpha", None),
        trail_frames=get("trail_frames", None),
        trail_decay=get("trail_decay", None),
        trail_blur=get("trail_blur", None),
        trail_dim=get("trail_dim", None),
        trail_blur_ramp=get("trail_blur_ramp", None),
        trail_blend=get("trail_blend", None),
        motion_blur_samples=get("motion_blur_samples", None),
        motion_blur_shutter=get("motion_blur_shutter", None),
    )

    logger.debug("Converted args to RenderConfig: expand=%.2f, note_scale=(%.2f, %.2f)",
                 config.expand_factor, config.note_scale_x, config.note_scale_y)

    return config


def setup_legacy_state(config: RenderConfig, resources: ResourceContext) -> None:
    """Update global state module for backward compatibility.

    During migration, old code still accesses the state module. This function
    updates state with values from the new config/context objects.

    Args:
        config: RenderConfig to copy to state
        resources: ResourceContext to copy to state

    Note:
        This is a temporary compatibility shim and should be removed in Phase 5.
    """
    logger.debug("Syncing config/context to legacy state module (compatibility mode)")

    # Update state with config values
    config.to_state_module(state)

    # Update state with resource values
    resources.to_state_module(state)


def create_resource_context(respack: Optional[Any] = None) -> ResourceContext:
    """Create a new ResourceContext, optionally with a respack.

    Args:
        respack: Optional respack to include

    Returns:
        ResourceContext instance
    """
    return ResourceContext(respack=respack)
