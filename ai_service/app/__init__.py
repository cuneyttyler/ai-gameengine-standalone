"""AI Orchestration Service for the AI-native game engine (docs/technical.md §10).

A local FastAPI sidecar the editor talks to over HTTP. It turns a
natural-language instruction plus the engine's reflection schema and a summary
of the open scene into a structured, validated proposal — never free-form
engine edits. The editor validates the proposal again against its live
registry, shows it in Diff Review, and only applies what the user accepts.
"""
