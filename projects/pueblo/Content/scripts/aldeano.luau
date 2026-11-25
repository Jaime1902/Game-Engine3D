-- Aldeano con diálogo: al interactuar (E o clic mirándolo) abre un cuadro de texto abajo de la pantalla.
-- Cada E pasa a la siguiente frase. Si "pregunta" está activada, la última frase ofrece dos respuestas
-- con botones (el ratón se suelta mientras tanto). Alejarse cierra el diálogo.
-- Las frases van en "frases", separadas por |.

local Aldeano = {}

Aldeano.params = {
	nombre = "Aldeano",
	frases = "Hola, viajero.|Hace dias que los lobos rondan el pueblo.|Me ayudaras a echarlos?",
	pregunta = true,
	distancia = 4, -- metros a los que el diálogo se cierra solo
}

local function partir(texto)
	local lista = {}
	for frase in string.gmatch(texto, "[^|]+") do
		table.insert(lista, frase)
	end
	return lista
end

local function cerrar(self)
	self.linea = nil
	ui.cursor(false)
end

function Aldeano.onStart(self)
	self.lista = partir(self.frases)
	self.linea = nil
end

function Aldeano.onInteract(self)
	if self.linea == nil then
		self.linea = 1
		self.entity:playSound("sounds/campana.wav", 0.15)
	elseif self.linea < #self.lista then
		self.linea += 1
	elseif not self.pregunta then
		cerrar(self)
	end
end

function Aldeano.onUpdate(self, dt)
	if self.linea == nil then
		return
	end
	local jugador = scene.player()
	if jugador and vector.magnitude(jugador.position - self.entity.position) > self.distancia then
		cerrar(self)
		return
	end

	-- Cuadro de diálogo en la parte baja de la pantalla (unidades: píxeles de la escena).
	local ancho, alto = ui.width(), ui.height()
	local x, y, w, h = 6, alto - 62, ancho - 12, 56
	ui.panel(x, y, w, h)
	ui.text(self.nombre, x + 6, y + 4, "#ffd66e")
	ui.text(ui.wrap(self.lista[self.linea], w - 12), x + 6, y + 4 + ui.lineHeight())

	local ultima = self.linea == #self.lista
	if ultima and self.pregunta then
		-- Respuestas con botones: el ratón queda libre hasta elegir.
		ui.cursor(true)
		if ui.button("Si", x + w - 92, y + h - 18, 40, 14) then
			log(self.nombre .. ": gracias!")
			scene.send("Hud", "mision", "Echar a los lobos")
			cerrar(self)
		elseif ui.button("No", x + w - 46, y + h - 18, 40, 14) then
			log(self.nombre .. ": vaya...")
			cerrar(self)
		end
	else
		ui.text(if ultima then "E: cerrar" else "E: seguir", x + w - 6, y + h - 15, "#9aa0b8", 1, "right")
	end
end

return Aldeano
