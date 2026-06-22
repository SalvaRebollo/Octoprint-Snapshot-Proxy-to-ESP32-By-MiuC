# Estructura modular

```text
JC4827W543_ESP32-S3_OctoPrintViewer/
├── JC4827W543_ESP32-S3_OctoPrintViewer.ino
├── touch.h
├── ESTRUCTURA.md
└── src/
    ├── core/
    │   ├── app_config.h
    │   ├── app_navigation.h
    │   ├── app_theme.h
    │   ├── app_theme.cpp
    │   ├── app_ui.h
    │   └── app_ui.cpp
    └── tabs/
        ├── counter/
        │   ├── tab_counter.h
        │   └── tab_counter.cpp
        ├── octoprint/
        │   ├── tab_octoprint.h
        │   ├── tab_octoprint.cpp
        │   ├── features/
        │   │   ├── octoprint_feature.h
        │   │   └── octoprint_feature.cpp
        │   └── services/
        │       ├── snapshot_service.h
        │       └── snapshot_service.cpp
        ├── settings/
        │   ├── tab_settings.h
        │   ├── tab_settings.cpp
        │   └── services/
        │       ├── wifi_manager.h
        │       └── wifi_manager.cpp
        └── domotica/
            ├── tab_domotica.h
            ├── tab_domotica.cpp
            ├── features/
            │   ├── domotica_types.h
            │   └── domotica_config.h
            └── services/
                ├── webhook_service.h
                └── webhook_service.cpp
```

Arduino compila recursivamente los archivos `.cpp` que están dentro de `src/`.

## Organización

- `src/core`: infraestructura compartida por toda la aplicación.
- `src/tabs/counter`: pantalla Contador.
- `src/tabs/octoprint`: pantalla y lógica propia de OctoPrint.
- `src/tabs/settings`: pantalla Ajustes y servicio Wi-Fi.
- `src/tabs/domotica`: pantalla Domótica, webhooks y configuración privada.

Cada tab conserva dentro de su carpeta sus propios `features/` y `services/`. Solo el código realmente compartido debe vivir en `src/core`.

En OctoPrint la responsabilidad está dividida:

- `features/octoprint_feature`: interfaz LVGL, configuración, persistencia en NVS y orquestación. Todo el código aquí se ejecuta en el hilo principal.
- `services/snapshot_service`: motor de captura. Descarga el JPEG del proxy, lo decodifica en PSRAM con doble buffer y lo entrega al hilo principal desde una tarea de fondo. Esa tarea nunca toca LVGL: solo deja el frame listo y `octoprint_feature` lo aplica.

## Categorías de Ajustes

La tab `Ajustes` funciona como índice y no contiene directamente los controles. Cada categoría abre una ventana independiente con cabecera y botón `VOLVER`.

Las categorías actuales son:

- `Apariencia`: modo claro/oscuro, color principal, altura de la barra de pestañas, monitor de rendimiento y visibilidad de cada pestaña.
- `WiFi`: estado de conexión, reconexión y gestión de redes guardadas.
- `OctoPrint`: acceso a los parámetros del snapshot. Este botón solo aparece cuando la pestaña OctoPrint está visible.

Para añadir futuras opciones, como Bluetooth, se crea una nueva página dentro de `tab_settings.cpp`, se añade su valor a `SettingsCategory` y se registra un botón en la pantalla índice.
## Tema claro y oscuro

`src/core/app_theme.h/.cpp` administra el tema general de LVGL. La preferencia se guarda en NVS dentro del namespace `appui`, usando la clave `dark`.

El botón de la pestaña Ajustes aplica el cambio de modo inmediatamente. El selector de color modifica el color principal usado por botones, sliders y la línea de la tab activa. Ambas preferencias se escriben en NVS únicamente cuando el usuario cambia su control. Si todavía no existe una preferencia guardada, se utiliza el modo oscuro con color azul.

En el mismo namespace `appui` se guardan también la altura de la barra (`tabheight`), el monitor de rendimiento (`showperf`), la visibilidad de cada pestaña (`tabcounter`, `tabdomotica`, `taboctoprint`) y la última pestaña activa (`lasttab`).

## Recordar la última pestaña

Al arrancar se restaura la última pestaña que se estaba usando. Se guarda un identificador lógico de la pestaña (`AppPage`), no su índice, en la clave `lasttab` del namespace `appui`. Reglas:

- La pestaña Ajustes nunca se guarda como última: al entrar en Ajustes se conserva la última pestaña normal usada.
- Solo se escribe en NVS cuando la pestaña activa cambia realmente.
- Al arrancar, si la pestaña guardada está compilada, visible y disponible, se abre. Si no, se abre la primera pestaña visible que no sea Ajustes; y si no hay ninguna, se abre Ajustes.
## Desactivar módulos

En `src/core/app_config.h`:

```cpp
#define APP_ENABLE_OCTOPRINT 0
#define APP_ENABLE_DOMOTICA 0
```

Cada opción elimina su tab y su lógica asociada del binario.

## Añadir otra tab

1. Crea `src/tabs/nueva_tab/`.
2. Añade `tab_nueva.h` y `tab_nueva.cpp`.
3. Si lo necesita, crea `features/` y `services/` dentro de esa misma carpeta.
4. Expón como mínimo `create(lv_obj_t *parent)`.
5. Registra la tab en `buildApplicationTabs()` (dentro del `.ino`), respetando el orden de creación. Ajustes se añade siempre la última.
6. Si necesita ejecución periódica, expón `loop()` y llámalo desde el `loop()` principal.

Las pestañas se reconstruyen al cambiar su visibilidad desde Ajustes. Si una pestaña conserva estado entre reconstrucciones, expón un `detachUi()` que solo invalide los punteros LVGL sin borrar ese estado (como hace `CounterTab`).

## Configurar acciones de Domótica

El archivo privado `src/tabs/domotica/features/domotica_config.h` contiene la lista real. Cada entrada tiene el texto que se mostrará en el botón y su URL:

```cpp
constexpr DomoticaWebhookConfig DOMOTICA_WEBHOOKS[] = {
  { "Luz del salon", "https://example.com/webhook-privado" },
  { "Persiana", "https://example.com/otro-webhook-privado" }
};
```

La pestaña Domótica crea automáticamente un botón por cada elemento. Para añadir, eliminar, ordenar o renombrar acciones solo hay que editar esa lista y volver a cargar el firmware.

## Seguridad

`src/tabs/domotica/features/domotica_config.h` forma parte del repositorio con valores de ejemplo genéricos (tipo `example.com`). La configuración real, con las URLs y tokens privados, se mantiene como una modificación local de ese mismo archivo que no se sube a GitHub.

Antes de publicar, comprueba que solo se suben los valores de ejemplo y que tus datos reales permanecen únicamente en tu copia local sin subir.

La tarea de fondo nunca modifica objetos LVGL. Todas las actualizaciones visuales se realizan desde el hilo principal.

## Conexión con el proxy OctoPrint

La dirección del proxy ya no está escrita en el firmware. Desde `OctoPrint > PARAMETROS` se configuran por separado:

- IP del servidor, vacía por defecto.
- Puerto, `30113` por defecto.

El endpoint mantiene la ruta fija `/snapshot-lite.jpg`. Los valores se guardan en el namespace NVS `octoview` con las claves `proxyip` y `proxyport`. Si la IP está vacía, la tarea de snapshots permanece detenida.
