# Joins the shader prelude and a shader body into one WGSL module.
file(READ ${PRELUDE} prelude)
file(READ ${BODY} body)
file(WRITE ${OUT} "${prelude}\n${body}")
