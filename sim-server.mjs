import http from "node:http";
import fs from "node:fs";
const html = fs.readFileSync("C:/Users/Ahm3d/Documents/webUI-STM_ESPCAR/waveshare-simulator.html");
const server = http.createServer((req, res) => {
  res.writeHead(200, { "Content-Type": "text/html", "Content-Length": html.length });
  res.end(html);
});
server.listen(8766, "127.0.0.1", () => console.log("Simulator at http://127.0.0.1:8766"));
