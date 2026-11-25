-- Interfaz del juego: vida, monedas, misión actual y un menú (Tab) con botones.
-- Ponlo en un objeto vacío llamado "Hud". Otros scripts le hablan con mensajes:
--   scene.send("Hud", "vida", -10)       suma (o resta) vida
--   scene.send("Hud", "moneda", 1)       suma monedas
--   scene.send("Hud", "mision", "texto") cambia la misión

local Hud = {}

Hud.params = {
	vidaMaxima = 100,
	monedas = 0,
}

function Hud.onStart(self)
	self.vida = self.vidaMaxima
	self.mision = ""
	self.aviso = 0 -- segundos que queda a la vista la misión nueva
	self.menu = false
end

function Hud.onMessage(self, evento, valor)
	if evento == "vida" then
		self.vida = math.clamp(self.vida + (valor or 0), 0, self.vidaMaxima)
	elseif evento == "moneda" then
		self.monedas += valor or 1
	elseif evento == "mision" then
		self.mision = valor or ""
		self.aviso = 3
	end
end

function Hud.onUpdate(self, dt)
	local ancho, alto = ui.width(), ui.height()

	-- Vida y monedas, arriba a la izquierda.
	ui.panel(4, 4, 86, 30)
	ui.text("Vida", 9, 7, "#f4f4f4")
	ui.bar(9, 21, 76, 6, self.vida / self.vidaMaxima, "#d94c4c", "#3a1c24")
	ui.text("Monedas " .. self.monedas, ancho - 6, 6, "#ffd66e", 1, "right")

	-- Misión: grande y centrada al recibirla, luego pequeña bajo la vida.
	if self.mision ~= "" then
		if self.aviso > 0 then
			self.aviso -= dt
			ui.text("Nueva mision", ancho / 2, 30, "#ffd66e", 1, "center")
			ui.text(self.mision, ancho / 2, 44, "#ffffff", 2, "center")
		else
			ui.text("> " .. self.mision, 6, 38, "#c8d0e8")
		end
	end

	-- Menú: Tab lo abre y suelta el ratón para pulsar los botones.
	if input.wasPressed("Tab") then
		self.menu = not self.menu
		ui.cursor(self.menu)
	end
	if self.menu then
		ui.rect(0, 0, ancho, alto, "#00000080")
		local w, h = 120, 74
		local x, y = math.floor((ancho - w) / 2), math.floor((alto - h) / 2)
		ui.panel(x, y, w, h)
		ui.text("Menu", x + w / 2, y + 5, "#ffd66e", 1, "center")
		if ui.button("Continuar", x + 10, y + 22, w - 20, 16) then
			self.menu = false
			ui.cursor(false)
		end
		if ui.button("Curar", x + 10, y + 44, w - 20, 16) then
			self.vida = self.vidaMaxima
		end
	end
end

return Hud
