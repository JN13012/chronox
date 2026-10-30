# Chronox

Chronox V0 est un utilitaire natif Windows 10/11 en C++20 qui mesure le temps
pendant lequel la session utilisateur est déverrouillée et le PC éveillé.
L’application s’exécute en console, sans service réseau, interface graphique ni
stockage persistant.

## Construire

Avec Visual Studio 2022 et le SDK Windows :

```powershell
cmake -S . -B build -A x64
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

Le prototype Windows est construit dans `build\src\chronox.exe`. Le noyau et ses
tests peuvent aussi être compilés sur un système non-Windows ; l’adaptateur
Windows ne peut être exécuté que sous Windows.

Voir [docs/SPEC.md](docs/SPEC.md) pour le périmètre, les règles de mesure et les
vérifications manuelles.
