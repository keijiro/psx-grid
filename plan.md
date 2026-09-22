# Purpose

This project is a preparatory step toward the eventual goal of porting Jacquard
to PlayStation. It validates a basic GUI.

# Jacquard Project

Jacquard is the Unity project located at `~/Projects/jacquard/main`.

# PlayStation Development Environment

Use `../psx-test` as a reference to build and use an equivalent environment in
this project.

# Implementation Scope

Validate only the most basic operations instead of porting all of Jacquard at
once.

- Display only the main Score Plane and support placing tiles on it.
- The goal is to evaluate how the interface feels. Implementing tile behavior
  is outside the scope of this prototype.

# Controls

The original Jacquard is designed primarily for touch input. Replacing that
interaction with gamepad input is the most significant difference in this
port.

The D-pad moves the cursor in four directions. The X button opens a menu for
the selected position, where the user can create a lane or tile and edit an
existing lane or tile.

Tile behavior is not implemented at this stage, so selecting a tile generally
only allows the user to delete it. Lane length must remain editable.
