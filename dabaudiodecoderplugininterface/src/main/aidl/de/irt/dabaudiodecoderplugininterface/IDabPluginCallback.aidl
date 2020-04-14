package de.irt.dabaudiodecoderplugininterface;

interface IDabPluginCallback {

    void decodedAudioData(in byte[] pcmData, in int samplerate, in int channels);
	void outputFormatChanged(in int sampleRate, in int chanCnt);
}
