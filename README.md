# PrintCam Deck / OctoPrint Snapshot Proxy

Small, local-first Flask application that retrieves an OctoPrint webcam snapshot, processes it in memory, and returns a configurable JPEG suitable for browsers and embedded displays such as the ESP32-S3 JC4827W543.

Pequeña aplicación Flask, pensada para uso local, que obtiene una captura de la webcam de OctoPrint, la procesa en memoria y devuelve un JPEG configurable para navegadores y pantallas integradas como la ESP32-S3 JC4827W543.

---

## English

### Project goal

The goal of this project is to provide a simple bridge between an OctoPrint webcam and clients that need a smaller or cropped image.

The application:

- Fetches the original snapshot from OctoPrint.
- Applies digital zoom and panning.
- Resizes and crops the result to the requested resolution.
- Compresses it as JPEG with configurable quality.
- Keeps all image processing in memory.
- Provides a browser interface for adjusting and previewing the image.
- Can display the direct MJPEG stream with browser-side zoom and panning.
- Is designed for a trusted local network, not for direct Internet exposure.

### How it works

```text
OctoPrint webcam
       |
       +--> Snapshot --> Flask + Pillow --> Crop / resize / JPEG --> Browser or ESP32
       |
       +--> MJPEG stream --> Direct browser preview --> CSS zoom / panning
```

Snapshot transformations are performed by the server. Stream zoom and panning are visual transformations performed by the browser and do not modify the original stream.

### Main features

- Interactive web viewer.
- Snapshot and direct-stream preview modes.
- Digital zoom from `1.0x` to `5.0x`.
- Horizontal and vertical crop positioning.
- Configurable JPEG quality.
- Resolution presets:
  - 320 × 240
  - 480 × 270
  - 640 × 360
  - 800 × 480
  - 1280 × 720
  - 1920 × 1080
- Custom output resolutions from 80 × 80 up to 10000 × 10000.
- Parameter-aware in-memory cache.
- Runtime configuration reload through the `Get from config` button.
- Resolution changes can be written back to `config.py`.
- Relative application URLs, without hardcoded proxy addresses in the frontend.

> Very large output images can require hundreds of megabytes of memory. Use resolutions close to 10000 × 10000 carefully.

### Configuration

Configuration is stored in `app/config.py`:

```python
OCTOPRINT_SNAPSHOT_URL = "http://OCTOPRINT_HOST:PORT/webcam/?action=snapshot"
DEFAULT_WIDTH = 480
DEFAULT_HEIGHT = 270
JPEG_QUALITY = 75
CACHE_MS = 500
REQUEST_TIMEOUT = 5
```

`PORT` may be configured in `config.py`, but the Docker Compose environment variable is preferred:

```yaml
environment:
  PORT: "30113"
```

At startup, environment variables take precedence over `config.py`. Internal defaults are used when an optional value is missing.

The `Get from config` button reloads the snapshot URL, default dimensions, JPEG quality, cache duration, and request timeout without restarting the container. Changing the listening port still requires recreating or restarting the service.

### Docker / Portainer

The included `docker-compose.yml`:

- Uses `python:3.12-slim`.
- Installs the Python dependencies at container startup.
- Mounts the application directory as read-only.
- Mounts only `config.py` as read-write so the resolution selector can update it.
- Restarts the service unless it is manually stopped.

Adjust the host volume path and published port to match your TrueNAS or Docker installation, then deploy the stack with Portainer or Docker Compose.

Because the `config.py` writable mount is part of the container definition, changing the volume configuration requires recreating or redeploying the container once.

### Endpoints

#### `GET /`

Interactive web interface with snapshot/stream selection, zoom, panning, JPEG quality, resolution presets, and documentation.

#### `GET /snapshot-lite.jpg`

Returns a processed JPEG.

| Parameter | Range | Description |
| --- | --- | --- |
| `w` | 80–10000 | Output width |
| `h` | 80–10000 | Output height |
| `q` | 30–95 | JPEG quality |
| `zoom` | 1.0–5.0 | Digital zoom |
| `x` | 0.0–1.0 | Horizontal crop center |
| `y` | 0.0–1.0 | Vertical crop center |
| `t` | Any | Optional browser cache-busting value; ignored by processing |

Example:

```text
/snapshot-lite.jpg?w=640&h=360&q=80&zoom=2.5&x=0.7&y=0.5
```

#### `GET /stream-direct`

Redirects the browser to the OctoPrint MJPEG stream derived from the configured snapshot URL.

#### `GET /healthz`

Returns:

```json
{"status": "ok"}
```

This checks that the Flask service is running; it does not verify webcam availability.

#### `POST /reload-config`

Reloads the runtime configuration from `config.py` and clears the image cache.

#### `POST /set-resolution`

Writes a new default width and height to `config.py`, reloads the configuration, and clears the cache.

Example JSON body:

```json
{
  "width": 1920,
  "height": 1080
}
```

### Security

This service has no authentication and includes endpoints that can modify `config.py`. Keep it on a trusted local network and do not expose it directly to the Internet.

---

## Español

### Objetivo del proyecto

El objetivo de este proyecto es proporcionar un puente sencillo entre la webcam de OctoPrint y clientes que necesitan una imagen más pequeña, comprimida o recortada.

La aplicación:

- Obtiene el snapshot original desde OctoPrint.
- Aplica zoom digital y desplazamiento.
- Redimensiona y recorta el resultado a la resolución solicitada.
- Lo comprime como JPEG con calidad configurable.
- Realiza todo el procesamiento de imagen en memoria.
- Proporciona una interfaz web para ajustar y previsualizar la imagen.
- Puede mostrar el stream MJPEG directo con zoom y desplazamiento visual en el navegador.
- Está diseñada para una red local de confianza, no para exponerla directamente a Internet.

### Funcionamiento

```text
Webcam de OctoPrint
       |
       +--> Snapshot --> Flask + Pillow --> Recorte / tamaño / JPEG --> Navegador o ESP32
       |
       +--> Stream MJPEG --> Vista directa en navegador --> Zoom / desplazamiento CSS
```

Las transformaciones del snapshot se realizan en el servidor. El zoom y desplazamiento del stream son transformaciones visuales realizadas por el navegador y no modifican el stream original.

### Funciones principales

- Visor web interactivo.
- Modos de previsualización de snapshot y stream directo.
- Zoom digital entre `1.0x` y `5.0x`.
- Posicionamiento horizontal y vertical del recorte.
- Calidad JPEG configurable.
- Resoluciones predefinidas:
  - 320 × 240
  - 480 × 270
  - 640 × 360
  - 800 × 480
  - 1280 × 720
  - 1920 × 1080
- Resoluciones personalizadas desde 80 × 80 hasta 10000 × 10000.
- Caché en memoria independiente para cada combinación de parámetros.
- Recarga de configuración mediante el botón `Get from config`.
- El selector de resolución puede guardar los cambios en `config.py`.
- Rutas relativas en el frontend, sin direcciones del proxy hardcodeadas.

> Las imágenes de salida muy grandes pueden necesitar cientos de megabytes de memoria. Utiliza con cuidado resoluciones cercanas a 10000 × 10000.

### Configuración

La configuración se encuentra en `app/config.py`:

```python
OCTOPRINT_SNAPSHOT_URL = "http://HOST_OCTOPRINT:PUERTO/webcam/?action=snapshot"
DEFAULT_WIDTH = 480
DEFAULT_HEIGHT = 270
JPEG_QUALITY = 75
CACHE_MS = 500
REQUEST_TIMEOUT = 5
```

El `PORT` puede declararse en `config.py`, aunque se recomienda configurarlo mediante Docker Compose:

```yaml
environment:
  PORT: "30113"
```

Al iniciar, las variables de entorno tienen prioridad sobre `config.py`. Si falta un valor opcional, se utiliza un valor interno predeterminado.

El botón `Get from config` vuelve a cargar la URL del snapshot, dimensiones, calidad JPEG, duración de caché y timeout sin reiniciar el contenedor. Cambiar el puerto de escucha sí requiere recrear o reiniciar el servicio.

### Docker / Portainer

El archivo `docker-compose.yml` incluido:

- Utiliza `python:3.12-slim`.
- Instala las dependencias de Python al arrancar el contenedor.
- Monta el directorio de la aplicación como solo lectura.
- Monta únicamente `config.py` con escritura para que el selector pueda actualizar la resolución.
- Reinicia el servicio salvo que se detenga manualmente.

Ajusta la ruta del volumen y el puerto publicado a tu instalación de TrueNAS o Docker y despliega la stack mediante Portainer o Docker Compose.

Como el montaje escribible de `config.py` forma parte de la definición del contenedor, cualquier cambio en los volúmenes requiere recrear o redesplegar el contenedor una vez.

### Endpoints

#### `GET /`

Interfaz web interactiva con selección entre snapshot y stream, zoom, desplazamiento, calidad JPEG, selector de resolución y documentación.

#### `GET /snapshot-lite.jpg`

Devuelve un JPEG procesado.

| Parámetro | Rango | Descripción |
| --- | --- | --- |
| `w` | 80–10000 | Ancho de salida |
| `h` | 80–10000 | Alto de salida |
| `q` | 30–95 | Calidad JPEG |
| `zoom` | 1.0–5.0 | Zoom digital |
| `x` | 0.0–1.0 | Centro horizontal del recorte |
| `y` | 0.0–1.0 | Centro vertical del recorte |
| `t` | Cualquiera | Valor opcional anticaché del navegador; no afecta al procesamiento |

Ejemplo:

```text
/snapshot-lite.jpg?w=640&h=360&q=80&zoom=2.5&x=0.7&y=0.5
```

#### `GET /stream-direct`

Redirige el navegador al stream MJPEG de OctoPrint derivado de la URL de snapshot configurada.

#### `GET /healthz`

Devuelve:

```json
{"status": "ok"}
```

Comprueba que el servicio Flask está funcionando, pero no verifica la disponibilidad de la cámara.

#### `POST /reload-config`

Recarga la configuración desde `config.py` y vacía la caché de imágenes.

#### `POST /set-resolution`

Guarda un nuevo ancho y alto predeterminados en `config.py`, recarga la configuración y vacía la caché.

Ejemplo de cuerpo JSON:

```json
{
  "width": 1920,
  "height": 1080
}
```

### Seguridad

El servicio no tiene autenticación e incluye endpoints capaces de modificar `config.py`. Mantenlo dentro de una red local de confianza y no lo expongas directamente a Internet.
