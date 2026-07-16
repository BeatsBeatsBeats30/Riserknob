# Riser Knob Pro — Build auf Windows (fuer FL Studio)

Einmalig installieren:
1. Visual Studio 2022 Community (kostenlos) — mit "Desktopentwicklung mit C++"
2. CMake (cmake.org) — beim Installieren "Add to PATH" anhaken

Dann in der Eingabeaufforderung (cmd):

    cd %USERPROFILE%\Downloads
    tar -xf RiserKnob.zip
    cd RiserKnob
    cmake -B build
    cmake --build build --config Release

Die fertige Datei liegt in:
    build\RiserKnob_artefacts\Release\VST3\Riser Knob.vst3

Diese nach C:\Program Files\Common Files\VST3\ kopieren, dann in
FL Studio: Options -> Manage plugins -> Find plugins.

Hinweis: Der Beat-Pump synct auf das Projekt-Tempo und laeuft nur
bei laufender Wiedergabe.
