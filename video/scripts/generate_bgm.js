const fs = require("fs");
const path = require("path");

const sampleRate = 44100;
const durationSeconds = 30;
const totalSamples = sampleRate * durationSeconds;
const numChannels = 2;
const bytesPerSample = 2; // 16-bit

const buffer = Buffer.alloc(44 + totalSamples * numChannels * bytesPerSample);

// Write WAV Header
buffer.write("RIFF", 0);
buffer.writeUInt32LE(36 + totalSamples * numChannels * bytesPerSample, 4);
buffer.write("WAVE", 8);
buffer.write("fmt ", 12);
buffer.writeUInt32LE(16, 16); // Subchunk1Size (16 for PCM)
buffer.writeUInt16LE(1, 20);  // AudioFormat (1 for PCM)
buffer.writeUInt16LE(numChannels, 22);
buffer.writeUInt32LE(sampleRate, 24);
buffer.writeUInt32LE(sampleRate * numChannels * bytesPerSample, 28); // ByteRate
buffer.writeUInt16LE(numChannels * bytesPerSample, 32); // BlockAlign
buffer.writeUInt16LE(bytesPerSample * 8, 34); // BitsPerSample
buffer.write("data", 36);
buffer.writeUInt32LE(totalSamples * numChannels * bytesPerSample, 40);

// Chord progression: 4 chords of 7.5 seconds each
const chords = [
  // Fmaj9: F2, C3, A3, C4, E4, G4
  [87.31, 130.81, 220.00, 261.63, 329.63, 392.00],
  // Cmaj7: C2, G2, E3, G3, B3, D4
  [65.41, 98.00, 164.81, 196.00, 246.94, 293.66],
  // Dm9: D2, A2, F3, A3, C4, E4
  [73.42, 110.00, 174.61, 220.00, 261.63, 329.63],
  // Am9: A2, E3, C4, E4, G4, B4
  [110.00, 164.81, 261.63, 329.63, 392.00, 493.88],
];

// Arpeggio notes per chord
const arps = [
  [523.25, 659.25, 783.99, 659.25], // C5, E5, G5, E5
  [493.88, 587.33, 659.25, 587.33], // B4, D5, E5, D5
  [523.25, 659.25, 698.46, 659.25], // C5, E5, F5, E5
  [493.88, 587.33, 659.25, 783.99], // B4, D5, E5, G5
];

let offset = 44;
for (let i = 0; i < totalSamples; i++) {
  const t = i / sampleRate;

  // Global fade in (1.5s) and fade out (3s)
  let masterEnv = 1;
  if (t < 1.5) {
    masterEnv = t / 1.5;
  } else if (t > 27.0) {
    masterEnv = Math.max(0, (30.0 - t) / 3.0);
  }

  // Chord index
  const chordIdx = Math.min(3, Math.floor(t / 7.5));
  const chordNotes = chords[chordIdx];
  const tChord = t % 7.5;

  // Chord envelope: gentle swell
  const chordEnv = Math.sin((tChord / 7.5) * Math.PI) * 0.4 + 0.6;

  // Synthesize chord pad
  let padLeft = 0;
  let padRight = 0;
  for (let n = 0; n < chordNotes.length; n++) {
    const freq = chordNotes[n];
    // Gentle detuned dual oscillator
    const osc1 = Math.sin(2 * Math.PI * freq * t);
    const osc2 = Math.sin(2 * Math.PI * (freq * 1.002) * t);
    const wave = (osc1 * 0.6 + osc2 * 0.4);
    
    // Stereo panning based on note register
    const pan = 0.3 + 0.4 * (n / chordNotes.length);
    padLeft += wave * (1 - pan);
    padRight += wave * pan;
  }
  padLeft = (padLeft / chordNotes.length) * chordEnv * 0.16;
  padRight = (padRight / chordNotes.length) * chordEnv * 0.16;

  // Subtle rhythmic pulse / soft kick warmth every 0.5s
  const beat = t % 0.5;
  const kickEnv = Math.exp(-beat * 18);
  const kick = Math.sin(2 * Math.PI * 55 * Math.exp(-beat * 12) * beat) * kickEnv * 0.08;

  // Gentle high sparkling arpeggios
  const arpNotes = arps[chordIdx];
  const arpStep = Math.floor(t * 2) % 4; // 8th notes
  const arpFreq = arpNotes[arpStep];
  const arpBeat = (t * 2) % 1;
  const arpEnv = Math.exp(-arpBeat * 5);
  const arpWave = Math.sin(2 * Math.PI * arpFreq * t) * arpEnv * 0.045;
  const arpPan = (arpStep % 2 === 0) ? 0.7 : 0.3;

  // Combine channels
  let sampleLeft = (padLeft + kick + arpWave * (1 - arpPan)) * masterEnv;
  let sampleRight = (padRight + kick + arpWave * arpPan) * masterEnv;

  // Soft clip limiter
  sampleLeft = Math.tanh(sampleLeft);
  sampleRight = Math.tanh(sampleRight);

  // Convert to 16-bit integer
  const intL = Math.max(-32768, Math.min(32767, Math.floor(sampleLeft * 32767)));
  const intR = Math.max(-32768, Math.min(32767, Math.floor(sampleRight * 32767)));

  buffer.writeInt16LE(intL, offset);
  buffer.writeInt16LE(intR, offset + 2);
  offset += 4;
}

const outPath = path.join(__dirname, "..", "public", "audio", "bgm.wav");
fs.writeFileSync(outPath, buffer);
console.log(`Generated ambient BGM at ${outPath} (${(buffer.length / 1024 / 1024).toFixed(2)} MB)`);
