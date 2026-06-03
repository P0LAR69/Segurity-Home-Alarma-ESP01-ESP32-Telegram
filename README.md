# Segurity Home - Sistema de Alarma con Telegram

**Dos versiones del mismo sistema de alarma:**

- **ESP-01 3V3** → Activación por **corte/restauración de alimentación**
- **ESP32** → Botón de pánico + **Deep Sleep** (ultra bajo consumo)

---

## 📌 Versiones disponibles

| Versión | Placa | Activación | Consumo | Uso recomendado |
|---------|-------|----------|--------|-----------------|
| **ESP01_3V3** | ESP-01 | Corte de 3.3V | Muy bajo (0mA apagado) | Puertas, ventanas, cajones, perimetral |
| **ESP32** | ESP32 | Botón de pánico (GPIO4) | Muy bajo (Deep Sleep) | Botón de emergencia, control manual |

---

## Características comunes

- Alertas por **Telegram** (Grupo + Administrador)
- Portal web completo con captive portal
- Configuración fácil (WiFi, Token, Chat IDs)
- Botón de pánico manual
- Reporte diario a las 18:00
- Código limpio y bien comentado

---

## Cómo usar

1. Elige la carpeta según tu placa:
   - `ESP01_3V3/` → para ESP-01
   - `ESP32/` → para ESP32 DevKit

2. Abre el archivo `.ino` en Arduino IDE
3. Configura las librerías requeridas
4. Sube el código
5. Conéctate al Access Point y configura desde el navegador

---

**Autor original:** Heraldo Rosero  
**Mejoras y organización:** Iván Dario

---

## Licencia

MIT License - Libre para uso personal y comercial.
