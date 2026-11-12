// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

// Run with VeroScriptVisualExample after enabling Audio and the SDL3 audio provider.
int main()
{
	VS::Audio::Service audio = VS::Audio::Service::Create("sdl3");
	array<double> samples = array<double>();
	for (int i = 0; i < 9600; ++i)
	{
		samples.push(i % 100 < 50 ? 0.1 : -0.1);
	}
	VS::Audio::Buffer sound = VS::Audio::Buffer::Create(48000, 1, samples);
	VS::Audio::Stream playback = audio.OpenStream(VS::Audio::Direction::Playback, 0u64, 48000, 1, 48000);
	playback.SetGain(0.25);
	playback.Queue(sound);
	playback.Flush();
	playback.SetPaused(false);
	// Poll device/default-route work on the owner thread while playback runs.
	for (int tick = 0; tick < 25; ++tick)
	{
		audio.PollEvent();
		std::this_thread::sleep(10);
	}
	playback.Close();
	audio.CollectRetired();
	return 0;
}
