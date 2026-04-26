# Design

## Scope

This document describes project-level decisions only.

Global engineering rules are defined by:
docs/LDS_C_Engineering_Standard.md

## Overview

LDS Terminal is a GTK4 + Libadwaita terminal built on VTE. The system keeps a
small, finite state surface so the terminal stays predictable and testable.

## Identity Model

The project uses a dual identity model:

- technical: `lds-terminal`
- visual: `Amanay`

Technical identity covers application ID, executable, icon name, schema ID,
resource prefix, and window matching. Visual identity covers UI text and other
user-facing strings.

## Invariants

- application-id: `bar.astware.lds-terminal`
- WM_CLASS: `LdsTerminal`
- Exec: `lds-terminal`
- UI name: `Amanay`

## Architecture

The core flow is:

1. parse command-line arguments
2. initialize runtime and application state
3. create the window shell
4. create the first terminal tab
5. spawn the shell process in VTE
6. handle search, links, settings, and focus transitions through callbacks
7. tear down tabs and windows deterministically

The terminal model is finite:

- one tab owns one `LdsTerminalTerm`
- each term has a primary VTE
- a secondary VTE exists only during a vertical split
- splits are vertical only
- no nested pane trees exist

## System Integration

- GNOME uses the application ID and desktop entry for shell matching.
- Wayland relies on the technical application identity remaining stable.
- X11 relies on `WM_CLASS` and `StartupWMClass` matching the technical name.

## Operational Rules

- `Amanay` stays in UI text only
- `lds-terminal` stays in technical identifiers only
- window focus, split promotion, and teardown must preserve ownership
- search and link handling remain bounded, local, and event-driven

## Current Status

The project is intentionally small but real: window shell, terminal core,
search, links, preferences, diagnostics, and lifecycle control are already in
place.
