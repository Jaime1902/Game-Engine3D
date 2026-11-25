-- Zona que suena cuando el jugador entra en ella (por ejemplo, al llegar arriba de la rampa).
-- Ponla en una entidad con Cuerpo fisico de tipo Zona.

local Campana = {}

Campana.params = {
	mensaje = "Has llegado arriba",
	volumen = 0.8,
}

function Campana.onTriggerEnter(self, otro)
	if otro == scene.player() then
		audio.play("sounds/campana.wav", self.volumen)
		log(self.mensaje)
	end
end

return Campana
