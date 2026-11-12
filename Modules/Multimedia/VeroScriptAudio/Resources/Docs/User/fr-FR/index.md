# VeroScriptAudio — Services audio

Audio fournit des contrats natifs indépendants et des liaisons `VS::Audio` facultatives. Utilisez `find_package(VeroScriptAudio 0.1.1 EXACT CONFIG REQUIRED)` avec `VeroScript::Audio`, ou `VeroScriptVisual` avec `COMPONENTS Audio`. Le contrat de base ne dépend ni de SDL, Threads, Platform, ni de Core. Les scripts exigent le Core correspondant et une fabrique de service fournie par l’hôte. Installez le fournisseur choisi pour le matériel ; SDL utilise `audio/sdl3`, contrat `veroscript.audio`, version 0.1.1.

Les requêtes de capacités, de périphériques, d’état des flux et de tampons immuables sont des méthodes const, utilisables dans une capture par valeur en lecture seule. Ajouter des échantillons, consommer les données enregistrées, modifier ou fermer un flux nécessite un récepteur modifiable ; utilisez `mutable` ou une capture par référence modifiable.

`COMPONENTS Bindings` → `VeroScript::AudioBindings` + `VeroScript::Core`.

## Tampons et lecture

Les échantillons entrelacés sont des float32 finis, de 1 à 384000 Hz et 1 à 8 canaux. Les tableaux `array<double>` sont convertis après vérification. Une trame contient un échantillon par canal ; limite de 64 MiB, compteurs/indices/budgets en `int64`. `Buffer::Create(rate,channels,samples)` crée une copie immuable avec `FrameCount/SampleRate/Channels`, `GetSample(index)` et `GetSamples()`. `Buffer::FromWav(array<uint8>)` décode sans périphérique le RIFF/WAVE petit-boutiste PCM 8/16/24/32 et IEEE flottant 32/64, en conservant l’ordre des canaux sans déduire leur position. WAV compressé, extensible/RF64/RIFX, MP3 et Ogg sont refusés.

```veroscript
VS::Audio::Service audio = VS::Audio::Service::Create("sdl3");
array<double> samples = {0.0, 0.1, -0.1, 0.0};
VS::Audio::Buffer buffer = VS::Audio::Buffer::Create(48000, 1, samples);
VS::Audio::Stream stream = audio.OpenStream(VS::Audio::Direction::Playback, 0u64, 48000, 1, 96000);
stream.Queue(buffer);
stream.SetGain(0.5);
stream.Flush();
stream.SetPaused(false);
```

Les flux démarrent en pause. L’ID zéro suit la sortie/entrée par défaut ; les autres IDs désignent une connexion physique de la bonne direction. SDL convertit fréquence/canaux et mélange les flux indépendants. `Queue(buffer)` exige le même format applicatif ; `QueueSamples(samples)` accepte directement un tableau. Une file pleine échoue avant envoi. `Flush()` libère l’anticipation du rééchantillonnage sans attendre la fin audible. `SetGain(0..16)`, `SetPaused(bool)`, `Clear()` et `Close()` idempotent ne modifient que ce flux.

## Enregistrement et durée de vie

Ouvrir `Direction::Recording`, reprendre, puis appeler `Read(maxFrames)` sans blocage. Le résultat est immuable, vide sans données ; le nombre est compris entre zéro et le budget. Le débordement natif efface l’audio non lu et conserve `Overflowed` jusqu’à `Clear()`, bornant données converties et brutes même sans interrogation du script. Aucun rappel de script sur le thread audio. `GetState()` retourne `PendingFrames/Gain/Paused/Connected/Overflowed` ; les trames en attente excluent la latence matérielle et du rééchantillonnage.

`GetDevices()` fournit `Count` et `Get(index)` ; champs : `Direction/DeviceId/Name/SampleRate/Channels`. `PollEvent()` retourne `Type/Device` : None, DeviceAdded, DeviceRemoved, DeviceChanged, TopologyReset. Les transitions entre deux interrogations peuvent se regrouper ; l’état initial vient seulement de l’énumération. Réénumérer après TopologyReset. Audio ne consomme pas les événements Platform et n’initialise ni vidéo ni manettes. Fermer/réouvrir un périphérique explicite déconnecté ; les routes par défaut suivent le backend.

Utilisez le thread propriétaire de l’application ; conservez runtime et fournisseur jusqu’au dernier `CollectRetired()`. La libération finale depuis un autre thread est différée. Le SDK déployé doit inclure un vrai pilote audio ; permissions, gestes navigateur et disponibilité matérielle restent des exigences de la cible.

Voir [VeroScriptSdl3 — Fondations — Audio](../../../../../Backend/VeroScriptSdl3/Resources/Docs/User/fr-FR/index.md#audio-event-pumping).

<!-- docs-navigation -->

---

[VeroScript](../../../../../../../Resources/Docs/User/fr-FR/index.md)

[en-US](../en-US/index.md) · [fr-FR](index.md) · [ja-JP](../ja-JP/index.md) · [ko-KR](../ko-KR/index.md) · [zh-CN](../zh-CN/index.md) · [zh-TW](../zh-TW/index.md)
