# Riser Knob Pro — Build auf dem Mac

Einmalig (falls noch nicht installiert):

    xcode-select --install
    brew install cmake

Dann:

    cd ~/Downloads
    unzip RiserKnob.zip
    cd RiserKnob
    cmake -B build -DCMAKE_BUILD_TYPE=Release
    cmake --build build --config Release

Das Plugin (VST3 + AU) wird automatisch nach
~/Library/Audio/Plug-Ins/ kopiert. DAW neu starten, Plugins scannen.
