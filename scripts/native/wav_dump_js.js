// Decodes a whole WAV file with web/audio.js's own parseWav / decodeImaBlocks /
// decodePcm16 (the code before "export class", run unchanged in a VM) and writes planar
// little-endian float32, for scripts/native/check_ima.sh.
// Usage: node wav_dump_js.js web/audio.js in.wav out.f32
const fs = require('fs');
const vm = require('vm');
const [, , audioJs, input, output] = process.argv;
const src = fs.readFileSync(audioJs, 'utf8');
const code = src.slice(0, src.indexOf('export class'));
const ctx = vm.createContext({ DataView, Float32Array, Math, Array, Object });
vm.runInContext(code + '\nthis.parseWav = parseWav; this.decodeImaBlocks = decodeImaBlocks; this.decodePcm16 = decodePcm16;', ctx);
const buf = fs.readFileSync(input);
const bytes = new Uint8Array(buf.buffer, buf.byteOffset, buf.byteLength);
const wav = ctx.parseWav(bytes);
if (!wav) {
  console.log('unsupported');
  process.exit(0);
}
const channels = wav.tag === 0x11 ? ctx.decodeImaBlocks(bytes, wav, 0, wav.blocks) : ctx.decodePcm16(bytes, wav, 0, wav.frames);
const parts = channels.map((c) => Buffer.from(c.buffer, c.byteOffset, c.byteLength));
fs.writeFileSync(output, Buffer.concat(parts));
console.log(`tag ${wav.tag}, ${wav.channels} ch, ${wav.rate} Hz, ${wav.frames} frames`);
