import fs from "fs";
import { parse as parseNBT } from "../prismarine-nbt/prismarine-nbt.min.js";
import * as THREE from "three";
import { program } from "commander";
import path from "path";
import { GLTFExporter, TextureLoader } from "node-three-gltf";
import { cwd } from "process";
import { createCanvas, loadImage } from "canvas";

// Ensure temp directory exists
const tempDir = path.join(process.cwd(), "temp");
if (!fs.existsSync(tempDir)) {
    fs.mkdirSync(tempDir, { recursive: true });
}

program
    .version("1.0.0")
    .description("Convert Java Edition .nbt files to .glb for Blender")
    .argument("<input>", "Input .nbt file")
    .argument("<output>", "Output .glb file")
    .action(async (input, output) => {
        try {
            console.log(`---- [📌] Program Logs Start Here ----\n`);
            console.log(`[🟡] 📂 Reading file: ${input}\n`);
            const buffer = fs.readFileSync(input);
            const { parsed } = await parseNBT(buffer);
            const root = parsed.value;

            const outputDir = path.dirname(output);
            if (!fs.existsSync(outputDir))
                fs.mkdirSync(outputDir, { recursive: true });

            // Extract size
            const size = root.size?.value?.value;
            if (
                !Array.isArray(size) ||
                size.length !== 3 ||
                size.some((v) => typeof v !== "number")
            ) {
                throw new Error(`[❌] Invalid size data: ${JSON.stringify(size)}`);
            }
            const [width, height, depth] = size;
            console.log(`[🔍] Structure size: ${width} x ${height} x ${depth}\n`);

            // Extract blocks list
            const blocks = root.blocks?.value?.value;
            if (!Array.isArray(blocks))
                throw new Error("[❌] Missing or malformed blocks list.");

            // Extract block palette
            const palette = root.palette?.value?.value;
            if (!Array.isArray(palette))
                throw new Error("[❌] Invalid or missing block_palette.");

            console.log(`[🔍] Extracted palette list: ${palette.length} blocks\n`);

            // Setup Three.js scene
            const scene = new THREE.Scene();
            const blockSize = 1;
            const geometry = new THREE.BoxGeometry(blockSize, blockSize, blockSize);

            // Process blocks using the new structure
            await processBlocks(scene, blocks, palette, geometry);

            // Export after all blocks are processed
            console.log(`[🟡] Exporting GLB...`);
            const exporter = new GLTFExporter();
            const glbBuffer = await exporter.parseAsync(scene, {
                binary: true,
            });
            fs.writeFileSync(output, glbBuffer);
            console.log(`[✅] Exported to ${output}\n`);
            console.log(`---- [📌] Program Logs End Here ----`);
        } catch (error) {
            console.error("[❌] Error processing .nbt file:", error);
        }
    });

const materialCache = new Map();

async function getMaterials(blockName) {
    blockName = blockName.replace("minecraft:", "");
    if (!materialCache.has(blockName)) {
        const materials = await getBlockMaterial(blockName);
        materialCache.set(blockName, materials);
    }
    return materialCache.get(blockName);
}

async function processBlocks(scene, blocks, palette, geometry) {
    let addedBlocks = 0;

    for (const block of blocks) {
        const paletteIndex = block.state?.value;
        if (paletteIndex === undefined || paletteIndex >= palette.length) continue;

        const blockData = palette[paletteIndex];
        const blockName = blockData.Name?.value;

        if (!blockName || blockName === "minecraft:air") {
            continue;
        }

        const material = await getMaterials(blockName);
        if (!material) {
            console.error(`[❌] Material loading failed for ${blockName}`);
            continue;
        }

        const mesh = new THREE.Mesh(geometry, material);

        // Get coordinates directly from pos
        const [x, y, z] = block.pos?.value?.value;
        mesh.position.set(x, y, z);
        
        scene.add(mesh);
        addedBlocks++;
    }

    console.log(`[🔍] Added ${addedBlocks} blocks to scene.\n`);
}

async function getBlockMaterial(blockName) {
    blockName = blockName.replace("minecraft:", "");
    const modelPath = path.join(cwd(), "assets", "minecraft", "models", "block", `${blockName}.json`);
    if (!fs.existsSync(modelPath)) {
        console.warn(`[⚠️] Model for ${blockName} not found, using default.`);
        return Array(6).fill(new THREE.MeshStandardMaterial({ color: 0xff0000 }));
    }
    const modelData = JSON.parse(fs.readFileSync(modelPath, "utf-8"));
    const textureMap = new Map(); // "east" => MeshTexture ...
    const materials = [];
    const addMaterials = () => {
        const faceMap = {
            up: 2,
            down: 3,
            north: 4,
            south: 5,
            east: 0,
            west: 1,
        };
        for (const [face, faceIndex] of Object.entries(faceMap)) {
            const texture = textureMap.get(face);
            materials[faceIndex] = new THREE.MeshStandardMaterial({
                map: texture,
            });
        }
    };
    if (modelData.textures) {
        for (const [key, textureName] of Object.entries(modelData.textures)) {
            let cleanTexture = textureName.replace("minecraft:", "");
            const texturePath = path.join(cwd(), "assets", "minecraft", "textures", `${cleanTexture}.png`);
            if (fs.existsSync(texturePath)) {
                const textureLoader = new TextureLoader();
                const texture = await textureLoader.loadAsync(texturePath);
                texture.magFilter = THREE.NearestFilter;
                texture.minFilter = THREE.NearestFilter;
                textureMap.set(key, texture); // "end", "bottom", etc => MeshTexture
            }
            else {
                console.error(`[❌] Missing texture: ${cleanTexture}.png`);
            }
        }
    }
    let parent = modelData.parent?.replace("minecraft:", "");
    let parentData = modelData;
    while (parent) {
        if (parentData.elements) {
            for (const [key, data] of Object.entries(parentData.elements[0].faces)) {
                const textureReference = data.texture.slice(1); // Remove '#'
                const texture = textureMap.get(textureReference);
                textureMap.set(key, texture);
            }
            break;
        }
        const parentPath = path.join(cwd(), "assets", "minecraft", "models", `${parent}.json`);
        parentData = JSON.parse(fs.readFileSync(parentPath, "utf-8"));
        if (parentData.textures) {
            for (const [key, _textureReference] of Object.entries(parentData.textures)) {
                const textureReference = _textureReference.slice(1); // Remove '#'
                const texture = textureMap.get(textureReference);
                textureMap.set(key, texture);
            }
        }
        parent = parentData.parent?.replace("minecraft:", "") ?? undefined;
    }
    addMaterials();
    return materials;
}

program.parse(process.argv);