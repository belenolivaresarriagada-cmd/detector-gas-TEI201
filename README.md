# Detector IoT de gas para cocinas
TEI201 · Universidad Adolfo Ibáñez · Avance 3

## Integrantes
- Elisa Rivas
- Belén Olivares
- Renata Veas
- Gaspar Garrido

## Proyecto
Prototipo académico que captura variaciones de señal asociadas a gases combustibles mediante MQ-5 y ESP32, con pantalla LCD, luz indicadora y dashboard web local. No utiliza buzzer. El DHT11 complementa la medición con temperatura y humedad.

## ODS
3 Salud y bienestar; 7 Energía asequible y no contaminante (uso responsable de combustibles); 9 Industria, innovación e infraestructura.

## Instalación y uso
Consultar el esquema de hardware. Antes de energizar, comprobar alimentación y adaptar los niveles I2C de 5 V a 3,3 V. Cargar el firmware original mediante Arduino IDE con las dependencias y configuración de red correspondientes. Consultar la dirección IP local del ESP32 para abrir el dashboard desde la misma red. Las instrucciones exactas de compilación dependen del firmware original, aún no adjunto a este paquete.

## Alcance
No es un detector certificado ni mide concentración calibrada en ppm. El informe documenta captura MQ-5 y dashboard; la evidencia aportada no acredita persistencia CSV, recepción Telegram ni la corrección final del DHT11. Actualizar con evidencias recientes si ya se verificaron. No liberar gas combustible cerca del sensor energizado.

## Carpetas
- firmware: incorporar código original sin secretos.
- hardware: esquema y BOM estimada.
- diseno-3d: incorporar archivos CAD originales.
- testing: resultados documentados y pruebas por verificar.
- docs: informe técnico original.
- presentacion: presentación editable y PDF.
