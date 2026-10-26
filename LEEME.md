# SpectroCheck

Espectrograma para Logic (y cualquier DAW con AU o VST3) para ver **hasta dónde llega un audio** y saber si tiene el **corte típico de MP3/AAC**. Lo ponés en una pista, le das play y abajo te dice, por ejemplo, *"Corte abrupto en 16.0 kHz · Típico de MP3 de 128 kbps"* o *"Sin corte: llega hasta 22.0 kHz"*.

El audio pasa sin tocarse: el plugin solo mira.

---

El plugin se compila en GitHub, en una Mac en la nube: vos no instalás nada de desarrollo. Funciona en Mac (Apple Silicon e Intel) como Audio Unit y VST3.

---

## 1. Subir el proyecto a GitHub (una sola vez)

1. Entrá a <https://github.com>, arriba a la derecha tocá **+** → **New repository**.
   - *Repository name:* `SpectroCheck`
   - Puede ser **Private** o **Public**.
   - **No** marques "Add a README" ni ninguna otra casilla.
   - Tocá **Create repository**.
2. En la página que aparece, tocá el link **uploading an existing file**.
3. En el Finder, abrí la carpeta `SpectroCheck` y apretá **Cmd + Shift + .** (punto) para ver los archivos ocultos, incluida la carpeta **.github** (es la que le dice a GitHub cómo compilar).
4. Seleccioná **todo** lo que hay adentro (Cmd + A), incluida `.github`, y arrastralo a la página de GitHub.
5. Esperá a que suba todo y tocá el botón verde **Commit changes**.

## 2. Esperar la compilación

1. Arriba en tu repositorio tocá la pestaña **Actions**.
2. Vas a ver "Compilar SpectroCheck" con un círculo amarillo girando. Tarda unos 10 a 15 minutos.
3. Cuando aparece un tilde verde ✅, está listo. Si aparece una cruz roja ❌, tocala, sacá una captura y mandámela.

## 3. Bajar e instalar el plugin

1. En la página principal del repositorio, a la derecha en **Releases**, tocá **SpectroCheck (última versión)**.
2. Bajá **SpectroCheck-mac.zip** y descomprimilo (doble clic): queda la carpeta **SpectroCheck-mac** en Descargas.
3. Abrí la **Terminal** (Cmd + Espacio, escribí "Terminal"), pegá esto y apretá Enter:

```
bash ~/Downloads/SpectroCheck-mac/instalar.command
```

Cuando dice **✅ Listo**, abrí Logic. Lo encontrás en **Audio Units > 0600musica > SpectroCheck**.

---

## Cómo usarlo

1. Poné SpectroCheck en la pista del audio que querés revisar (o en el master).
2. Dale play. A los pocos segundos aparece el veredicto abajo.
3. La medición se reinicia sola, así cada archivo se mide por separado:
   - **cada vez que le das Play** en Logic;
   - **después de 2 segundos de silencio**;
   - **cuando el audio cambia mientras suena** (por ejemplo, si pasás de un WAV a un MP3 con solo o mute): si los huecos de los últimos 5 segundos no coinciden para nada con lo medido antes, empieza de nuevo.

   También podés tocar **Reiniciar** cuando quieras. Abajo, al lado del total de huecos, el valor **"ahora"** muestra solo los últimos 5 segundos.

### Qué significan los colores del veredicto

| Color | Qué quiere decir |
|---|---|
| 🟠 Naranja | Corte abrupto: la señal se termina de golpe. Es la marca de un archivo con pérdida (MP3, AAC, Ogg). |
| 🟢 Verde | Banda completa: llega hasta el final, o el corte está tan arriba que es del conversor, no de un códec. |
| 🔵 Azul | Se apaga de a poco: es la grabación o la mezcla, no un códec. |
| ⚪ Gris | Esperando señal. |

### Dónde suele cortar cada calidad (orientativo)

| Corte | Típico de |
|---|---|
| ~16 kHz | MP3 96–128 kbps |
| ~17.5 kHz | MP3 160 kbps |
| ~19 kHz | MP3 192 kbps |
| ~19.5–20 kHz | MP3 256 kbps, AAC, Ogg alta calidad |
| ~20–20.5 kHz | MP3 320 kbps |
| hasta 22 kHz (a 44.1) o 24 kHz (a 48) | WAV/FLAC sin pérdida |

Cada codificador corta en un lugar un poco distinto, así que tomalo como guía. El de Logic, en 192 kbps o más, casi no corta: para esos está el detector de huecos. Un caso especial: si el proyecto está a 88.2 o 96 kHz y el audio corta en ~22 kHz, es un archivo de "alta resolución" hecho desde uno de 44.1/48 kHz.

### Huecos en los agudos (MP3 de alta calidad sin corte)

Algunos codificadores, como el exportador de MP3 de Logic, casi no cortan los agudos en 192, 256 o 320 kbps, así que el corte solo no alcanza para descubrirlos. Pero el codificador **borra los agudos de a ratos** (en pedacitos de ~12 ms) cuando no le alcanzan los bits, sobre todo en el estéreo (canal lateral). Un WAV original nunca hace eso: sus agudos son continuos.

- La franja **HUECOS**, arriba del espectrograma, se pinta de **naranja** en cada momento en que los agudos se apagan de golpe. En un WAV original queda gris.
- Abajo a la derecha dice qué porcentaje del tiempo se apagaron, en el estéreo y en el centro.
- Desde un 6% el veredicto pasa a *"Huecos en los agudos: probable MP3/AAC"*. Entre 1.5% y 6% avisa *"algunos huecos"*.
- Con un master 24/48 y sus versiones MP3 de Logic dio: WAV original 0.2%, MP3 320 32%, MP3 256 52% (igual que el "falso WAV" hecho desde el 256), MP3 192 82%.
- En un tema mono el estéreo no se puede medir y solo se usa el centro, así que no da falsos positivos.

### Líneas fijas

Debajo del veredicto, SpectroCheck avisa si hay **tonos angostos que se mantienen durante todo el audio**. Para contar, el tono tiene que verse en el 80% del tiempo o más y sobresalir sobre las frecuencias vecinas. Empieza a informar después de 5 segundos con sonido.

- Se marcan en turquesa sobre el espectrograma, con la frecuencia a la izquierda, y con un punto en el perfil.
- Si la línea está **por encima del corte** de un MP3, se marca en naranja con el aviso *"posible MP3 disfrazado"*. Es la típica señal de alguien que le agregó algo a un archivo con pérdida para que parezca de banda completa.
- También aparecen cosas como el zumbido eléctrico (50/60 Hz), el silbido de 15.7 kHz de equipos viejos o tonos de marca de agua.
- Las líneas fijas no se tienen en cuenta al medir el corte, así que un tono suelto en 19 kHz no hace que un MP3 de 16 kHz parezca de banda completa.

### Los controles

- **Escala:** *Lineal* (recomendada para ver cortes: reparte los agudos con mucho espacio), *Log* (como un analizador común) o *Agudos* (solo de 10 kHz para arriba, para mirar el corte de cerca).
- **FFT:** 4096, 8192 o 16384. Más grande es más preciso en frecuencia y más borroso en el tiempo.
- **Velocidad:** qué tan rápido corre la imagen.
- **Rango:** qué nivel se ve como negro (-100, -120 o -140 dB).
- **Canal:** L + R, izquierdo, derecho o **Lateral (S)**. Muchos MP3 cortan el estéreo más abajo que el centro: si en L + R no ves nada raro, mirá el Lateral.
- **Guías:** líneas punteadas en 16, 19, 20 y 22 kHz.
- **Promedio:** muestra u oculta el panel de promedio y picos de la derecha. Oculto, el espectrograma usa todo el ancho.
- **Congelar:** detiene la imagen y la medición.
- Pasando el mouse por encima ves la frecuencia exacta y el nivel promedio en ese punto.
- A la derecha, el **perfil** muestra el espectro promedio y los picos de toda la medición. El corte de un MP3 se ve como una caída vertical.

### Tené en cuenta

- Si Logic convierte el archivo a la frecuencia del proyecto, el plugin ve la versión convertida. Para revisar un archivo, conviene que el proyecto esté a la misma frecuencia que el archivo.
- Un archivo sin corte no garantiza que sea original: alguien puede haber convertido un MP3 a WAV. Pero un corte abrupto a 16–20 kHz casi siempre delata que en algún momento fue un archivo con pérdida.
- La medición corre mientras la ventana del plugin está abierta.

## Cuando haya una versión nueva

1. En tu repositorio de GitHub tocá **Add file** → **Upload files**.
2. Arrastrá **todo** el contenido de la carpeta nueva (con Cmd + Shift + . activado, para que entre `.github`).
3. Tocá **Commit changes**, esperá el tilde verde en **Actions** y repetí el paso 3.
