# Releases

## 2026-04-04

### main_application
- Version: 1.0.0
- Scope: Current stable C++ runtime and mode orchestration state.
- Tag: main_application-v1.0.0

### rpi_pico_code
- Version: 1.0.0
- Scope: Current stable CircuitPython sensor streaming state.
- Tag: rpi_pico_code-v1.0.0

## Create tags and push

```bash
git tag -a main_application-v1.0.0 -m "main_application v1.0.0"
git tag -a rpi_pico_code-v1.0.0 -m "rpi_pico_code v1.0.0"
git push origin main_application-v1.0.0 rpi_pico_code-v1.0.0
```

## Create GitHub releases

- Open repository Releases page.
- Create release from tag `main_application-v1.0.0`.
- Create release from tag `rpi_pico_code-v1.0.0`.
