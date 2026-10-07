const { MsEdgeTTS, OUTPUT_FORMAT } = require("msedge-tts");
const fs = require("fs");
const path = require("path");

const clips = [
  {
    filename: "voice_scene1.mp3",
    text: "CppDesk. Standalone remote desktop for Windows. Zero setup. Just connect.",
  },
  {
    filename: "voice_scene2.mp3",
    text: "Connect in three seconds. Enter a nine-digit ID, and you're in.",
  },
  {
    filename: "voice_scene3.mp3",
    text: "Fluid sixty frames per second control. Smooth, responsive, multi-monitor freedom.",
  },
  {
    filename: "voice_scene4.mp3",
    text: "Drag and drop file sharing, remote terminal, screen privacy, and live voice chat.",
  },
  {
    filename: "voice_scene5.mp3",
    text: "You're always in full control. Fast, secure, and open source. Get CppDesk today.",
  },
];

async function generateAll() {
  const tts = new MsEdgeTTS();
  // Using en-US-AvaNeural with subtle pitch reduction and calm pacing for a sultry, warm, intimate tone
  await tts.setMetadata("en-US-AvaNeural", OUTPUT_FORMAT.AUDIO_24KHZ_48KBITRATE_MONO_MP3);

  const outDir = path.join(__dirname, "..", "public", "audio");
  if (!fs.existsSync(outDir)) {
    fs.mkdirSync(outDir, { recursive: true });
  }

  for (const clip of clips) {
    const targetPath = path.join(outDir, clip.filename);
    console.log(`Generating ${clip.filename}...`);
    await new Promise((resolve, reject) => {
      const { audioStream } = tts.toStream(clip.text, {
        pitch: "-4Hz",
        rate: "-3%",
      });
      const out = fs.createWriteStream(targetPath);
      audioStream.pipe(out);
      out.on("finish", () => {
        const stat = fs.statSync(targetPath);
        const durationSec = (stat.size / 6000).toFixed(2);
        console.log(`Saved ${clip.filename} (${stat.size} bytes, ~${durationSec}s)`);
        resolve();
      });
      audioStream.on("error", reject);
      out.on("error", reject);
    });
  }

  console.log("All calm voiceover clips generated successfully!");
}

generateAll().catch((err) => {
  console.error("Error generating voiceovers:", err);
  process.exit(1);
});
