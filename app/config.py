# Configuration file - DO NOT COMMIT YOUR HOSTNAME OR PORT TO GITHUB
# Copy config.example.py to config.py and adjust values

# Flask server port
# PORT = 30113

# OctoPrint snapshot URL
OCTOPRINT_SNAPSHOT_URL = "http://<OCTOPRINT_IP>:<OCTOPRINT_PORT>/webcam/?action=snapshot"

# Default image dimensions (pixels)
DEFAULT_WIDTH = 480
DEFAULT_HEIGHT = 270

# JPEG compression quality (0-100, higher = better quality, larger file)
JPEG_QUALITY = 95

# Cache duration in milliseconds (how long to serve cached image without refetching)
CACHE_MS = 500

# Request timeout in seconds (timeout for HTTP requests to OctoPrint)
REQUEST_TIMEOUT = 5
