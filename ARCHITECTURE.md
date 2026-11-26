# Architecture

The PE loader maps title modules and binds imports to runtime shims. The runtime supplies memory, storage, audio and Xbox D3D12 interfaces; the graphics layer translates commands to Vulkan and shader code to SPIR-V. The host process owns thread and TLS state. Game files remain external; writes use an overlay. See docs/victory_architecture_reference.md for architectural comparison.
