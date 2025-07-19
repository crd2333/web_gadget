import fs from "fs";
import { parse as parseNBT } from "./dist/prismarine-nbt.min.js";

async function main(file) {
  const buffer = fs.readFileSync(file)
  const { parsed, type } = await parseNBT(buffer)
  const jsonStr = JSON.stringify(parsed, null, 2)
  console.log('JSON serialized', jsonStr)
  fs.writeFileSync('output.json', jsonStr) // 保存为 output.json
}
main('./bedrock.mcstructure')
