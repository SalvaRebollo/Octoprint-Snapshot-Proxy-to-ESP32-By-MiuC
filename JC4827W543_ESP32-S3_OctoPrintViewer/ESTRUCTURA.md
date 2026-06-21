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
    │   ├── app_ui.h
    │   └── app_ui.cpp
    └── tabs/
        ├── counter/
        │   ├── tab_counter.h
        │   └── tab_counter.cpp
        ├── octoprint/
        │   ├── tab_octoprint.h
        │   ├── tab_octoprint.cpp
        │   └── features/
        │       ├── octoprint_feature.h
        │       └── octoprint_feature.cpp
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
            │   ├── domotica_config.h
            │   └── domotica_config.example.h
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
5. Registra la tab en `createApplicationUi()`.
6. Si necesita ejecución periódica, expón `loop()` y llámalo desde el `loop()` principal.

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

`domotica_config.h` contiene los tokens privados y está excluido mediante `.gitignore`. `domotica_config.example.h` es la plantilla pública que sí puede subirse a GitHub.

Antes de publicar, comprueba que el archivo privado continúa ignorado y que nunca se ha añadido previamente al historial de Git.

La tarea de fondo nunca modifica objetos LVGL. Todas las actualizaciones visuales se realizan desde el hilo principal.