# 🚀 MATRIX EVENTOS - ESP32

<p align="center">
  <img src="https://img.shields.io/badge/ESP32-IoT-blue?style=for-the-badge&logo=espressif" />
  <img src="https://img.shields.io/badge/C%2B%2B-Arduino-orange?style=for-the-badge&logo=cplusplus" />
  <img src="https://img.shields.io/badge/Estado-Activo-brightgreen?style=for-the-badge" />
  <img src="https://img.shields.io/badge/Web-Control-purple?style=for-the-badge" />
  <img src="https://img.shields.io/badge/License-MIT-green?style=for-the-badge" />
</p>

---

## 🧠 Descripción

Sistema basado en **ESP32** que permite gestionar eventos desde una interfaz web y mostrarlos en una matriz LED RGB en tiempo real.

💡 Pensado para:

* Cartelería digital
* Recordatorios
* Sistemas embebidos interactivos

---

## ⚙️ Características

* 🔐 Autenticación con token
* 🧾 CRUD completo de eventos
* 💾 Persistencia en EEPROM
* 🎨 Colores personalizados
* 🌈 Color dinámico automático
* 📺 Scroll en matriz LED
* ⏰ Control por horario
* 📡 Reconexión WiFi automática
* 🌐 Interfaz web responsive

---

## 🌐 Acceso al sistema

Podés acceder desde cualquier navegador dentro de la red:

http://<IP_DEL_ESP32>

📌 Ejemplo:
http://192.168.125.191

---

## 🔐 Credenciales por defecto

Usuario: admin
Contraseña: 1p3t.2026

⚠️ Recomendación: cambiar en el código antes de usar en producción.

---

## 🧩 Funcionalidades principales

* 🔑 Iniciar sesión
* 🔒 Cerrar sesión
* ➕ Agregar evento
* ✏️ Editar evento
* 🗑 Eliminar evento
* 👀 Visualizar eventos

---

## 🧱 Arquitectura del sistema

| Componente       | Descripción              |
| ---------------- | ------------------------ |
| Event            | Representa cada evento   |
| Auth             | Manejo de autenticación  |
| Storage          | Guarda datos en EEPROM   |
| WebServer        | Maneja rutas HTTP        |
| MatrixDisplay    | Controla la matriz LED   |
| SystemController | Coordina todo el sistema |

---

## 📦 Estructura del proyecto
```bash
📁 matrix-eventos-esp32
┣ 📄 matrix-eventos-esp32.ino
┣ 📄 README.md
┗ 📄 LICENSE
```
---

## ⚙️ Configuración
```cpp
#define ACTIVE_HOUR_START 6
#define ACTIVE_HOUR_END   20
```
🕒 Fuera de ese horario:

* LEDs apagados
* Web sigue funcionando

---

## 🔥 Ejemplo de visualización

CUMPLE JUAN 3 DIAS
EXAMEN HOY!

---

## 💾 Almacenamiento

* Uso de memoria EEPROM
* Persistencia tras reinicio
* Límite de eventos según memoria

---

## 📡 Conectividad

* Conexión WiFi automática
* Reintentos en caso de fallo

---

## 🚀 Posibles mejoras

* API REST
* App móvil
* Animaciones LED
* Multiusuario
* Base de datos externa

---

## 👨‍💻 Autor

Santino Massera

---

## 📜 Licencia

Este proyecto está bajo la licencia MIT.

---


