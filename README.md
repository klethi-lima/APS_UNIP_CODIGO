# APS_UNIP_CODIGO

Projeto acadêmico em C++ para visualização e ordenação de focos de incêndio.

## Requisitos

- CMake 3.15 ou superior
- Compilador C++17
- Pacotes CMake de `imgui`, `SFML 3` e `ImGui-SFML`

## Compilar

Com as dependências instaladas e disponíveis para o CMake:

```powershell
cmake -S . -B build
cmake --build build --config Debug
```

Os arquivos gerados ficam em `build/` e não precisam ser enviados ao repositório.
