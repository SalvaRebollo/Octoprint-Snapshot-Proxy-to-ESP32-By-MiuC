import os
import time
from io import BytesIO

import requests
from flask import Flask, Response, request
from PIL import Image, ImageOps

app = Flask(__name__)

# Load configuration from config.py, with environment variable overrides
from config import (
    PORT as CONFIG_PORT,
    OCTOPRINT_SNAPSHOT_URL as CONFIG_SNAPSHOT_URL,
    DEFAULT_WIDTH as CONFIG_WIDTH,
    DEFAULT_HEIGHT as CONFIG_HEIGHT,
    JPEG_QUALITY as CONFIG_QUALITY,
    CACHE_MS as CONFIG_CACHE_MS,
    REQUEST_TIMEOUT as CONFIG_TIMEOUT,
)

# Environment variables can override config.py (useful in Docker)
PORT = int(os.environ.get("PORT", str(CONFIG_PORT)))
OCTOPRINT_SNAPSHOT_URL = os.environ.get("OCTOPRINT_SNAPSHOT_URL", CONFIG_SNAPSHOT_URL)
DEFAULT_WIDTH = int(os.environ.get("DEFAULT_WIDTH", str(CONFIG_WIDTH)))
DEFAULT_HEIGHT = int(os.environ.get("DEFAULT_HEIGHT", str(CONFIG_HEIGHT)))
JPEG_QUALITY = int(os.environ.get("JPEG_QUALITY", str(CONFIG_QUALITY)))
CACHE_MS = int(os.environ.get("CACHE_MS", str(CONFIG_CACHE_MS)))
REQUEST_TIMEOUT = float(os.environ.get("REQUEST_TIMEOUT", str(CONFIG_TIMEOUT)))

cache = {}


def clamp(value, minimum, maximum):
    return max(minimum, min(maximum, value))

def get_cache_key(width, height, quality, zoom, cx, cy):
    return f"{width}x{height}_q{quality}_z{zoom}_x{cx}_y{cy}"

def jpeg_response(jpg_bytes):
    response = Response(jpg_bytes, mimetype="image/jpeg")
    response.headers["Cache-Control"] = "no-store, no-cache, must-revalidate, max-age=0"
    response.headers["Pragma"] = "no-cache"
    response.headers["Expires"] = "0"
    return response


def crop_zoom(img, zoom, cx, cy):
    """
    zoom = 1.0 -> imagen completa
    zoom = 2.0 -> recorte 2x
    cx/cy = centro del recorte, valores 0.0 a 1.0
    """
    if zoom <= 1.0:
        return img

    w, h = img.size

    crop_w = int(w / zoom)
    crop_h = int(h / zoom)

    center_x = int(w * cx)
    center_y = int(h * cy)

    left = center_x - crop_w // 2
    top = center_y - crop_h // 2

    left = clamp(left, 0, w - crop_w)
    top = clamp(top, 0, h - crop_h)

    right = left + crop_w
    bottom = top + crop_h

    return img.crop((left, top, right, bottom))


@app.route("/")
def index():
    return """
    <html>
      <head>
        <title>OctoPrint Snapshot Proxy</title>
        <meta name="viewport" content="width=device-width, initial-scale=1.0">

        <style>
          body {
            font-family: sans-serif;
            background: #111;
            color: #eee;
            margin: 0;
            padding: 16px;
          }

          h1 {
            font-size: 22px;
            margin: 0 0 12px 0;
          }

          .viewer {
            width: 100%;
            max-width: 960px;
            margin-bottom: 16px;
          }

          #cam {
            width: 100%;
            border: 2px solid #444;
            background: #000;
            display: block;
          }

          .panel {
            max-width: 960px;
            display: grid;
            gap: 12px;
          }

          .controls {
            display: grid;
            grid-template-columns: repeat(2, minmax(0, 1fr));
            gap: 10px;
          }

          .row {
            background: #1e1e1e;
            border: 1px solid #333;
            border-radius: 8px;
            padding: 10px;
          }

          label {
            display: block;
            font-size: 13px;
            margin-bottom: 4px;
            color: #bbb;
          }

          input[type="range"] {
            width: 100%;
          }

          button,
          a.button {
            border: 0;
            border-radius: 8px;
            padding: 12px;
            background: #333;
            color: #fff;
            font-size: 15px;
            cursor: pointer;
            text-align: center;
            text-decoration: none;
            display: block;
          }

          button:hover,
          a.button:hover {
            background: #444;
          }

          .primary {
            background: #255c99;
          }

          .shortcut {
            background: #7a4ca0;
          }

          .buttons {
            display: grid;
            grid-template-columns: repeat(3, 1fr);
            gap: 8px;
          }

          .url {
            font-size: 12px;
            color: #aaa;
            word-break: break-all;
            background: #1e1e1e;
            padding: 10px;
            border-radius: 8px;
            border: 1px solid #333;
          }

          .legend {
            max-width: 960px;
            background: #1e1e1e;
            border: 1px solid #444;
            border-radius: 8px;
            padding: 15px;
            margin-top: 20px;
            font-size: 13px;
          }

          .legend h2 {
            margin: 0 0 12px 0;
            font-size: 16px;
            color: #fff;
          }

          .legend h3 {
            margin: 12px 0 8px 0;
            font-size: 13px;
            color: #aaa;
            text-transform: uppercase;
            letter-spacing: 0.5px;
          }

          .legend code {
            background: #000;
            color: #00d4ff;
            padding: 2px 4px;
            border-radius: 3px;
            font-family: monospace;
            font-size: 12px;
          }

          .legend ul {
            margin: 6px 0;
            padding-left: 20px;
          }

          .legend li {
            margin: 4px 0;
            line-height: 1.4;
          }

          .param-table {
            width: 100%;
            border-collapse: collapse;
            margin: 8px 0 12px 0;
            font-size: 12px;
          }

          .param-table th,
          .param-table td {
            border: 1px solid #333;
            padding: 6px 8px;
            text-align: left;
          }

          .param-table th {
            background: #2a2a2a;
            color: #00d4ff;
            font-weight: bold;
          }

          .param-table td {
            background: #0a0a0a;
          }

          @media (max-width: 700px) {
            .controls {
              grid-template-columns: 1fr;
            }

            .buttons {
              grid-template-columns: repeat(2, 1fr);
            }

            .param-table {
              font-size: 11px;
            }

            .param-table th,
            .param-table td {
              padding: 4px 6px;
            }
          }
        </style>
      </head>

      <body>
        <h1>OctoPrint Snapshot Proxy</h1>

        <div class="viewer">
          <img id="cam" src="/snapshot-lite.jpg">
        </div>

        <div class="panel">
          <div class="controls">
            <div class="row">
              <label>Zoom: <span id="zoomValue">1.0</span></label>
              <input id="zoom" type="range" min="1" max="5" step="0.1" value="1">
            </div>

            <div class="row">
              <label>X / horizontal: <span id="xValue">0.50</span></label>
              <input id="x" type="range" min="0" max="1" step="0.01" value="0.5">
            </div>

            <div class="row">
              <label>Y / vertical: <span id="yValue">0.50</span></label>
              <input id="y" type="range" min="0" max="1" step="0.01" value="0.5">
            </div>

            <div class="row">
              <label>Calidad JPEG: <span id="qValue">75</span></label>
              <input id="q" type="range" min="30" max="95" step="1" value="75">
            </div>
          </div>

          <div class="buttons">
            <button onclick="resetView()">Reset</button>
            <button onclick="zoomOut()">Zoom -</button>
            <button onclick="zoomIn()">Zoom +</button>

            <button onclick="moveLeft()">← Izquierda</button>
            <button onclick="moveUp()">↑ Arriba</button>
            <button onclick="moveRight()">Derecha →</button>

            <button class="primary" onclick="updateImage()">Actualizar</button>
            <button onclick="moveDown()">↓ Abajo</button>
            <button class="shortcut" onclick="shortcutNozzle()">Shortcut 2.6x</button>
          </div>

          <a class="button shortcut" href="#" onclick="openCurrentDirect(); return false;">
            Abrir vista actual
          </a>

          <div class="url" id="currentUrl"></div>

          <div class="legend">
            <h2>📖 Documentación de URLs Disponibles</h2>

            <h3>Endpoints</h3>
            <ul>
              <li><code>/</code> - Página principal interactiva con viewer y controles</li>
              <li><code>/snapshot-lite.jpg</code> - Obtener snapshot con parámetros personalizados</li>
              <li><code>/healthz</code> - Health check del servicio</li>
            </ul>

            <h3>Parámetros de /snapshot-lite.jpg</h3>
            <table class="param-table">
              <thead>
                <tr>
                  <th>Parámetro</th>
                  <th>Rango</th>
                  <th>Por Defecto</th>
                  <th>Descripción</th>
                </tr>
              </thead>
              <tbody>
                <tr>
                  <td><code>w</code></td>
                  <td>80 - 1280</td>
                  <td>480</td>
                  <td>Ancho de la imagen en píxeles</td>
                </tr>
                <tr>
                  <td><code>h</code></td>
                  <td>80 - 720</td>
                  <td>270</td>
                  <td>Alto de la imagen en píxeles</td>
                </tr>
                <tr>
                  <td><code>q</code></td>
                  <td>30 - 95</td>
                  <td>75</td>
                  <td>Calidad JPEG (30-95)</td>
                </tr>
                <tr>
                  <td><code>zoom</code></td>
                  <td>1.0 - 5.0</td>
                  <td>1.0</td>
                  <td>Factor de zoom (1.0 = imagen completa)</td>
                </tr>
                <tr>
                  <td><code>x</code></td>
                  <td>0.0 - 1.0</td>
                  <td>0.5</td>
                  <td>Centro horizontal del recorte (0=izquierda, 1=derecha)</td>
                </tr>
                <tr>
                  <td><code>y</code></td>
                  <td>0.0 - 1.0</td>
                  <td>0.5</td>
                  <td>Centro vertical del recorte (0=arriba, 1=abajo)</td>
                </tr>
              </tbody>
            </table>

            <h3>URLs Disponibles</h3>
            <ul>
              <li><code>/</code> - Página principal interactiva</li>
              <li><code>/snapshot-lite.jpg</code> - Snapshot por defecto (480x270, zoom 1.0)</li>
              <li><code>/snapshot-lite.jpg?w=640&h=360&q=90</code> - Imagen 16:9 de mayor resolución y calidad</li>
              <li><code>/snapshot-lite.jpg?zoom=2.5&x=0.5&y=0.5</code> - Zoom 2.5x en el centro</li>
              <li><code>/snapshot-lite.jpg?zoom=3&x=0.8&y=0.6&q=85&w=640&h=360</code> - Zoom con posición personalizada</li>
              <li><code>/snapshot-lite.jpg?zoom=2.6&x=1&y=0.7</code> - Shortcut nozzle/cama</li>
              <li><code>/healthz</code> - Health check del servicio</li>
            </ul>

            <h3>Notas</h3>
            <ul>
              <li>Todos los parámetros son opcionales</li>
              <li>Los valores fuera de rango serán ajustados automáticamente</li>
              <li>El cache se refresca cada 500ms</li>
              <li>La imagen se redimensiona manteniendo su proporción original</li>
              <li>Las rutas son relativas para que funcionen aunque cambie la IP, el puerto o uses proxy inverso</li>
            </ul>
          </div>
        </div>

        <script>
          const cam = document.getElementById("cam");

          const zoomInput = document.getElementById("zoom");
          const xInput = document.getElementById("x");
          const yInput = document.getElementById("y");
          const qInput = document.getElementById("q");

          const zoomValue = document.getElementById("zoomValue");
          const xValue = document.getElementById("xValue");
          const yValue = document.getElementById("yValue");
          const qValue = document.getElementById("qValue");
          const currentUrl = document.getElementById("currentUrl");

          function clamp(value, min, max) {
            return Math.max(min, Math.min(max, value));
          }

          function getImageUrl() {
            const zoom = parseFloat(zoomInput.value);
            const x = parseFloat(xInput.value);
            const y = parseFloat(yInput.value);
            const q = parseInt(qInput.value);

            const params = new URLSearchParams({
              w: "480",
              h: "270",
              q: q.toString(),
              zoom: zoom.toFixed(1),
              x: x.toFixed(2),
              y: y.toFixed(2),

              // Este parámetro evita que el navegador reutilice una imagen antigua.
              // Luego lo quitamos de la URL visible para que no moleste.
              t: Date.now().toString()
            });

            return "/snapshot-lite.jpg?" + params.toString();
          }

          function updateLabels() {
            zoomValue.textContent = parseFloat(zoomInput.value).toFixed(1);
            xValue.textContent = parseFloat(xInput.value).toFixed(2);
            yValue.textContent = parseFloat(yInput.value).toFixed(2);
            qValue.textContent = qInput.value;
          }

          function updateImage() {
            updateLabels();

            const relativeUrl = getImageUrl();
            cam.src = relativeUrl;

            const cleanUrl = new URL(relativeUrl, window.location.origin);
            cleanUrl.searchParams.delete("t");

            currentUrl.textContent = cleanUrl.toString();
          }

          function openCurrentDirect() {
            const relativeUrl = getImageUrl();
            const cleanUrl = new URL(relativeUrl, window.location.origin);
            cleanUrl.searchParams.delete("t");

            window.open(cleanUrl.toString(), "_blank");
          }

          function resetView() {
            zoomInput.value = "1";
            xInput.value = "0.5";
            yInput.value = "0.5";
            qInput.value = "75";
            updateImage();
          }

          function shortcutNozzle() {
            zoomInput.value = "2.6";
            xInput.value = "1";
            yInput.value = "0.7";
            qInput.value = "75";
            updateImage();
          }

          function zoomIn() {
            zoomInput.value = clamp(parseFloat(zoomInput.value) + 0.2, 1, 5).toFixed(1);
            updateImage();
          }

          function zoomOut() {
            zoomInput.value = clamp(parseFloat(zoomInput.value) - 0.2, 1, 5).toFixed(1);
            updateImage();
          }

          function moveLeft() {
            xInput.value = clamp(parseFloat(xInput.value) - 0.05, 0, 1).toFixed(2);
            updateImage();
          }

          function moveRight() {
            xInput.value = clamp(parseFloat(xInput.value) + 0.05, 0, 1).toFixed(2);
            updateImage();
          }

          function moveUp() {
            yInput.value = clamp(parseFloat(yInput.value) - 0.05, 0, 1).toFixed(2);
            updateImage();
          }

          function moveDown() {
            yInput.value = clamp(parseFloat(yInput.value) + 0.05, 0, 1).toFixed(2);
            updateImage();
          }

          zoomInput.addEventListener("input", updateImage);
          xInput.addEventListener("input", updateImage);
          yInput.addEventListener("input", updateImage);
          qInput.addEventListener("input", updateImage);

          setInterval(updateImage, 1000);

          updateImage();
        </script>
      </body>
    </html>
    """


@app.route("/healthz")
def healthz():
    return {"status": "ok"}


@app.route("/snapshot-lite.jpg")
def snapshot_lite():
    width = int(request.args.get("w", DEFAULT_WIDTH))
    height = int(request.args.get("h", DEFAULT_HEIGHT))
    quality = int(request.args.get("q", JPEG_QUALITY))

    zoom = float(request.args.get("zoom", "1.0"))
    cx = float(request.args.get("x", "0.5"))
    cy = float(request.args.get("y", "0.5"))

    width = clamp(width, 80, 1280)
    height = clamp(height, 80, 720)
    quality = clamp(quality, 30, 95)
    zoom = clamp(zoom, 1.0, 5.0)
    cx = clamp(cx, 0.0, 1.0)
    cy = clamp(cy, 0.0, 1.0)

    key = get_cache_key(width, height, quality, zoom, cx, cy)
    now = time.time() * 1000

    if key in cache:
        cached_time, cached_bytes = cache[key]
        if now - cached_time < CACHE_MS:
            return jpeg_response(cached_bytes)

    try:
        r = requests.get(OCTOPRINT_SNAPSHOT_URL, timeout=REQUEST_TIMEOUT)
        r.raise_for_status()

        img = Image.open(BytesIO(r.content)).convert("RGB")

        img = crop_zoom(img, zoom, cx, cy)

        # Recorta/ajusta manteniendo proporción para llenar exactamente la salida.
        img = ImageOps.fit(
            img,
            (width, height),
            method=Image.Resampling.LANCZOS,
            centering=(0.5, 0.5)
        )

        output = BytesIO()
        img.save(output, format="JPEG", quality=quality, optimize=True)
        jpg_bytes = output.getvalue()

        cache[key] = (now, jpg_bytes)

        return jpeg_response(jpg_bytes)

    except Exception as e:
        msg = f"Error creando snapshot-lite: {e}"
        return Response(msg, status=500, mimetype="text/plain")


if __name__ == "__main__":
    app.run(host="0.0.0.0", port=PORT)