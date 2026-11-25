-- Puerta que se abre y se cierra al hacer clic sobre ella en Play.
-- Gira alrededor de su bisagra (el borde izquierdo de la pieza, mirando por -Z).
-- Los valores de params aparecen en el Inspector y se pueden cambiar por entidad.

local Puerta = {}

Puerta.params = {
	velocidad = 90, -- grados por segundo
	angulo = 90, -- cuánto se abre, en grados
	abierta = false, -- estado al empezar
}

local function girar(self)
	local ancho = self.entity.scale.x
	local radianes = math.rad(self.base + self.actual)
	-- El centro de la pieza queda a medio ancho de la bisagra, en la dirección de su eje X girado.
	local direccion = vector.create(math.cos(radianes), 0, -math.sin(radianes))
	self.entity.position = self.bisagra + direccion * (ancho / 2)
	self.entity.rotation = vector.create(self.inclinacion.x, self.base + self.actual, self.inclinacion.z)
end

function Puerta.onStart(self)
	local rotacion = self.entity.rotation
	self.base = rotacion.y
	self.inclinacion = rotacion
	local radianes = math.rad(self.base)
	local eje = vector.create(math.cos(radianes), 0, -math.sin(radianes))
	self.bisagra = self.entity.position - eje * (self.entity.scale.x / 2)
	self.objetivo = if self.abierta then self.angulo else 0
	self.actual = self.objetivo
	girar(self)
end

function Puerta.onInteract(self)
	self.abierta = not self.abierta
	self.objetivo = if self.abierta then self.angulo else 0
	log(if self.abierta then "se abre" else "se cierra")
end

function Puerta.onUpdate(self, dt)
	if self.actual == self.objetivo then
		return
	end
	local paso = self.velocidad * dt
	if self.actual < self.objetivo then
		self.actual = math.min(self.actual + paso, self.objetivo)
	else
		self.actual = math.max(self.actual - paso, self.objetivo)
	end
	girar(self)
end

-- Otro script puede abrirla con: scene.send("Puerta", "abrir")
function Puerta.onMessage(self, evento)
	if evento == "abrir" and not self.abierta then
		Puerta.onInteract(self)
	elseif evento == "cerrar" and self.abierta then
		Puerta.onInteract(self)
	end
end

return Puerta
