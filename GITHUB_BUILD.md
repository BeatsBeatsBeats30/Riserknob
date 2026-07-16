# Windows-VST3 ohne Windows-PC bauen (GitHub Actions)

GitHub kompiliert das Plugin kostenlos auf einem Windows-Server.
Einmal einrichten, danach bei jeder Code-Aenderung nur noch hochladen.

## 1. GitHub-Account (falls noch keiner da)
github.com -> Sign up (kostenlos)

## 2. Repository anlegen
Oben rechts "+" -> "New repository" -> Name z.B. "riser-knob"
-> "Private" waehlen -> "Create repository"

## 3. Code hochladen
Auf der Repo-Seite: "uploading an existing file" klicken.
Im Finder den RiserKnob-Ordner oeffnen und ALLE Dateien/Ordner
(CMakeLists.txt, Source, die .md-Dateien) ins Browserfenster ziehen.
Unten "Commit changes" klicken.

## 4. Workflow-Datei anlegen (der versteckte .github-Ordner
   kommt beim Drag & Drop meist nicht mit, deshalb von Hand):
"Add file" -> "Create new file"
Als Dateiname exakt eingeben:  .github/workflows/build.yml
Inhalt: den kompletten Inhalt der Datei .github/workflows/build.yml
aus diesem Projekt reinkopieren (im Finder: Cmd+Shift+Punkt zeigt
versteckte Ordner). "Commit changes".

## 5. Bauen lassen
Tab "Actions" oben im Repo -> der Build "Build Windows VST3"
startet automatisch (gelber Punkt = laeuft, gruener Haken = fertig,
dauert ca. 5-10 Minuten).

## 6. Fertige Datei runterladen
Auf den gruenen Build klicken -> unten bei "Artifacts" liegt
"RiserKnob-Windows-VST3" -> runterladen, entpacken.
Darin: "Riser Knob.vst3" — das ist die fertige Windows-Datei.

## 7. An den Kollegen schicken
Er kopiert die "Riser Knob.vst3" nur noch nach:
C:\Program Files\Common Files\VST3\
Dann FL Studio: Options -> Manage plugins -> Find installed plugins.
Fertig — er muss NICHTS installieren oder bauen.
