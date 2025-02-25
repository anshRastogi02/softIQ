import express from 'express';
import fs from 'fs';
import fetch from 'node-fetch';
import os from 'os';
import path from 'path';
import multer from 'multer';
import moment from 'moment';
import OpenAI from 'openai';
import { fileURLToPath } from "url";
import { ElevenLabsClient } from 'elevenlabs';

const app = express();
const PORT = 3000;
const ELEVEN_LABS_API_KEY = "21de";
const openai = new OpenAI({ apiKey: 'sNr4A' });
const voiceId = "k0IXsdJ59XctG7kP1dFW";

const __filename = fileURLToPath(import.meta.url);
const __dirname = path.dirname(__filename);

let newResponse = 0;

const client = new ElevenLabsClient({
    apiKey: ELEVEN_LABS_API_KEY,
});



async function chat(prompt) {
    const startTime = Date.now();
    console.log(`[${moment().format()}] 📢 Generating AI response for: "${prompt}"`);
    const response = await openai.chat.completions.create({
        model: 'gpt-4o-mini',
        messages: [
            { role: 'system', content: 'Role: Baby Ganesh, गणेश, भगवान गणेश, a divine young Indian Hindu god... (same as original system message)' },
            { role: 'user', content: prompt }
        ],
        temperature: 1.25,
        max_tokens: 20
    });
    const aiResponse = response.choices[0].message.content.trim();
    const latency = Date.now() - startTime;
    console.log(`[${moment().format()}] ✅ AI Response: "${aiResponse}" (Latency: ${latency} ms)`);
    return aiResponse;
}



async function stt(wavFilePath) {
    const startTime = Date.now();
    const audioFile = fs.createReadStream(wavFilePath);
    console.log(`[${moment().format()}] 🎤 Processing file: ${wavFilePath}`);

    try {
        const response = await openai.audio.transcriptions.create({
            model: 'whisper-1',
            file: audioFile,
            response_format: 'text',
            language: 'en' // Default language is English
        });

        let recognizedText = response.trim();
        const latency = Date.now() - startTime;
        
        console.log(`[${moment().format()}] ✅ Recognized Text: "${recognizedText}" (Latency: ${latency} ms)`);

        // Ensure response is only in Hindi or English
        if (!/^[\u0900-\u097F\sA-Za-z0-9.,!?'"()-]*$/.test(recognizedText)) {
            console.log(`[${moment().format()}] ⚠️ Invalid Language Detected!`);
            return "Error: Transcription contains unsupported language.";
        }

        return recognizedText;
    } catch (error) {
        console.error(`[${moment().format()}] ❌ STT Error: ${error.message}`);
        return "Error: Could not process the audio file.";
    }
}



async function streamTTS(text, res) {
    const startTime = Date.now();
    console.log(`[${moment().format()}] 🔊 Streaming TTS`);

    const url = `https://api.elevenlabs.io/v1/text-to-speech/${voiceId}/stream`;

    const requestOptions = {
        method: "POST",
        headers: {
            "Content-Type": "application/json",
            "xi-api-key": ELEVEN_LABS_API_KEY,
        },
        body: JSON.stringify({
            text: text,
            model_id: "eleven_monolingual_v1",
            voice_settings: { stability: 0.5, similarity_boost: 0.5 },
        }),
    };

    try {
        const response = await fetch(url, requestOptions);
        if (!response.ok) {
            throw new Error(`HTTP error! Status: ${response.status}`);
        }

        // Set headers for streaming audio
        res.setHeader("Content-Type", "audio/mpeg");

        // Stream the Eleven Labs response directly to the client
        response.body.pipe(res);

        const latency = Date.now() - startTime;
        console.log(`[${moment().format()}] ✅ TTS Streamed (Latency: ${latency} ms)`);
    } catch (error) {
        console.error("Error streaming TTS:", error);
        res.status(500).send("Error streaming TTS");
    }
}

// async function streamTTS(text, res) {
//     const startTime = Date.now();
//     console.log(`[${moment().format()}] 🔊 Streaming TTS`);

//     const url = `https://api.elevenlabs.io/v1/text-to-speech/${voiceId}/stream`;

//     const requestOptions = {
//         method: "POST",
//         headers: {
//             "Content-Type": "application/json",
//             "xi-api-key": ELEVEN_LABS_API_KEY,
//         },
//         body: JSON.stringify({
//             text: text,
//             model_id: "eleven_monolingual_v1",
//             voice_settings: { stability: 0.5, similarity_boost: 0.5 },
//         }),
//     };


//     try {
//         const response = await fetch(url, requestOptions);
//         if (!response.ok) {
//             throw new Error(`HTTP error! Status: ${response.status}`);
//         }

//         // Set headers for streaming audio
//         res.setHeader("Content-Type", "audio/mpeg");

//         // Stream the Eleven Labs response directly to the client
//         response.body.pipe(res);

//         const latency = Date.now() - startTime;
//         console.log(`[${moment().format()}] ✅ TTS Streamed (Latency: ${latency} ms)`);
//     } catch (error) {
//         console.error("Error streaming TTS:", error);
//         res.status(500).send("Error streaming TTS");
//     }
// }



// Get Host IP Address


function getHostIP() {
    const interfaces = os.networkInterfaces();
    for (const key in interfaces) {
        for (const iface of interfaces[key]) {
            if (iface.family === 'IPv4' && !iface.internal) {
                return iface.address;
            }
        }
    }
    return '127.0.0.1';
}

app.use(express.json());
app.use(express.urlencoded({ extended: true }));

const storage = multer.memoryStorage();
const upload = multer({ storage });

function createWavFile(audioBuffer, sampleRate, bitDepth, channels) {
    const numSamples = audioBuffer.length / (bitDepth / 8);
    const byteRate = (sampleRate * channels * (bitDepth / 8));
    const blockAlign = channels * (bitDepth / 8);
    const dataSize = audioBuffer.length;

    const header = Buffer.alloc(44);
    
    header.write('RIFF', 0);
    header.writeUInt32LE(36 + dataSize, 4);  
    header.write('WAVE', 8);


    header.write('fmt ', 12);
    header.writeUInt32LE(16, 16);  
    header.writeUInt16LE(1, 20); 
    header.writeUInt16LE(channels, 22);  
    header.writeUInt32LE(sampleRate, 24);
    header.writeUInt32LE(byteRate, 28);
    header.writeUInt16LE(blockAlign, 32);
    header.writeUInt16LE(bitDepth, 34);

    // "data" sub-chunk
    header.write('data', 36);
    header.writeUInt32LE(dataSize, 40);

    // Combine header and audio data
    return Buffer.concat([header, audioBuffer]);
}

let latestText = "";

app.post('/upload', upload.single('audio'), (req, res) => {
    console.log("📤 [UPLOAD] Received an audio file...");
    latestText = "";

    if (req.headers['transfer-encoding'] !== 'chunked') {
        console.error('Expected chunked transfer encoding, but received:', req.headers['transfer-encoding']);
        return res.status(400).send('Expected chunked transfer encoding');
    }

    // Read audio parameters from headers
    const sampleRate = parseInt(req.headers['x-audio-sample-rates']) || 44100;
    const bitDepth = parseInt(req.headers['x-audio-bits']) || 16;
    const channels = parseInt(req.headers['x-audio-channel']) || 2;
    console.log(`Audio Info - Sample Rate: ${sampleRate}, Bit Depth: ${bitDepth}, Channels: ${channels}`);

    const timestamp = moment().utc().format('YYYYMMDDTHHmmss[Z]');
    const wavFilename = `audio_rec/${timestamp}_${sampleRate}_${bitDepth}_${channels}.wav`;
    const wavFilePath = path.join(__dirname, wavFilename);

    console.log(`🎵 [UPLOAD] File saved as WAV: ${wavFilePath}`);
    let totalBytes = 0;
    let audioData = [];

    req.on('data', (chunk) => {
        console.log(`Received chunk of size: ${chunk.length}`);
        totalBytes += chunk.length;
        audioData.push(chunk);
    });

    req.on('end', async () => {
        const audioBuffer = Buffer.concat(audioData);

        // Prepare WAV file
        const wavBuffer = createWavFile(audioBuffer, sampleRate, bitDepth, channels);
        
        fs.writeFile(wavFilePath, wavBuffer, (err) => {
            if (err) {
                console.error('Error writing file:', err);
                return res.status(500).send('Error writing file');
            }
            
            console.log(`File ${wavFilename} written successfully with ${totalBytes} bytes.`);
        });
        

        // Processing the audio
        const userText = await stt(wavFilePath); 
        if (userText) {
            const aiResponse = await chat(userText);
            latestText = aiResponse
        }

        // **Set newResponse flag to 1**
        newResponse = 1;
        console.log("✅ New response ready for streaming!");
        
        res.send({ status: "AI RESPONSE GENERATED"});

    });

    req.on('error', (err) => {
        console.error('Error receiving data:', err);
        res.status(500).send('Error receiving file');
    });
});

// app.get("/stream", async (req, res) => {
//     const text = latestText || "The Response did not generated please try again.";
//     const startTime = Date.now();
//     await streamTTS(text, res);
//     const latency = Date.now() - startTime;
// });


app.get("/stream", async (req, res) => {
    if (newResponse === 0) {
        return res.status(204).end(); // No content, ESP should retry later
    }
    newResponse = 0; // Reset flag as the ESP has started receiving data
    const startTime = Date.now();
    const text = latestText || "Hello, I'm Ansh Rastogi from IIT Roorkee, It is the text to test.";

    try {
        const audioStream = await client.generate({
            voice: voiceId,
            model_id: 'eleven_turbo_v2_5',
            text,
        });

        res.setHeader("Content-Type", "audio/mpeg");

        const chunks = [];
        for await (const chunk of audioStream) {
            const latency = Date.now() - startTime; // Calculate latency
            console.log(`API latency: ${latency} ms`); // Log the latency
            console.log(`Received chunk of size: ${chunk.length} bytes`); // Log chunk size
            chunks.push(chunk);
            res.write(chunk); // Stream the chunk to the response
        }

        res.end(); // End the response after streaming all chunks
    } catch (error) {
        console.error("Error streaming speech:", error);
        res.status(500).send("Error streaming speech");
    }
});

app.get('/status', (req, res) => {
    res.setHeader('Content-Type', 'text/plain');
    res.send(newResponse ? '1' : '0');
});


app.get('/', (req, res) => {
    console.log("📄 [HOME] Serving the home page.");
    res.send(
        `<h1>🎉 Welcome to the Node.js Audio Server!</h1>
        <p>✅ Server is up and running.</p>
        <ul>
            <li><a href="/stream">🎵 Stream Latest MP3</a></li>
            <li>📤 Use <b>POST /upload</b> to upload a WAV file</li>
            <li> Check <a href="/status">Status</a> of the Response </li> 
        </ul>`
    );
});

const IP = getHostIP();
app.listen(PORT, IP, () => {
    console.log(`🚀 Server is running at: http://${IP}:${PORT}`);
});
