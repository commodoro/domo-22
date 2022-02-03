#! /usr/bin/python3

from bottle import run, route, request, response, static_file
import json
import time
import sqlite3


def writeLog(client, code, room, msg):
    """Escribe el log en formato csv."""
    with open('registro.log', 'a') as f:
        f.write(','.join((time.ctime(), client, code, room, msg)))
        f.write('\n')


@route("/acceso", method="POST")
def acceso():
    """POST /acceso \n
    Obtiene un csv del log."""

    # REST
    response.add_header("Content-Type", "application/json")
    if ('client' not in request.json) or ('code_access' not in request.json) or ('room' not in request.json):
        response.status = 400
        response.body = 'Te faltan datos'
        return response

    # Extraer datos
    client: str = request.json['client']
    code: str = request.json['code_access'].lower()
    # print(code)
    room: str = request.json['room']

    # Conectar base de datos
    db = sqlite3.connect('datos.db')
    cur = db.cursor()

    # Consulta en tabla masters
    data_master = cur.execute(f'SELECT id FROM masters WHERE id="{code}"')
    if(cur.fetchone()):  # Esto significa que aparecemos en masters
        response.body = json.dumps({"acceso": True})
        writeLog(client, code, room, '"Acceso como maestro"')
        return response

    # Consulta en tabla accesos
    try:
        data = [
            *cur.execute(f'SELECT {room} FROM acceso_salas WHERE id="{code}"')]
        assert not cur.fetchone()
        assert len(data) > 0
        data = data[0][0]
    except (sqlite3.OperationalError, AssertionError):
        response.body = json.dumps({"acceso": False})
        writeLog(client, code, room, '"Error en la búsqueda"')
        return response
    if data == 0:  # No acceso
        response.body = json.dumps({"acceso": False})
        writeLog(client, code, room, '"Denegado"')
    elif data == 1:  # Acceso
        response.body = json.dumps({"acceso": True})
        if "lock" in request.json:
            # Si tengo lock y es True, dejo pasar y cierro.
            if request.json['lock']:
                cur.execute(
                    f'UPDATE acceso_salas SET {room}=2 WHERE id="{code}"')
                writeLog(client, code, room, '"Acceso, cierre"')
            else:
                writeLog(client, code, room, '"Acceso"')
    elif data == 2:
        if "lock" in request.json:  # Si tengo lock
            if request.json['lock']:  # Si es True, está cerrado, denegado
                response.body = json.dumps({"acceso": False})
                writeLog(client, code, room, '"Denegado, cerrado"')
            else:  # Si es False, abro, pasa
                response.body = json.dumps({"acceso": True})
                cur.execute(
                    f'UPDATE acceso_salas SET {room}=1 WHERE id="{code}"')
                writeLog(client, code, room, '"Acceso, apertura"')
        else:
            response.body = json.dumps({"acceso": False})
            writeLog(client, code, room, '"Denegado, sin llave"')
    else:
        response.body = json.dumps({"acceso": False})
        writeLog(client, code, room,
                 '"Error en la base de datos, valor distinto de 0-2"')

    # Cierre
    db.commit()
    db.close()
    return response


@route("/log")
def getLog():
    """GET /log \n
    Obtiene un csv del log."""

    # Texto
    response.add_header("Content-Type", "text/html; charset=UTF-8")

    # Depende de la query
    with open('registro.log', "r") as f:
        if 'n' in request.query:
            n = int(request.query['n'])
            return "".join(f.readlines()[:n])
        else:
            return "".join(f.readlines())


@route("<filename:path>")
def servidor_estatico(filename):
    """GET /<filename> \n
    Servidor de archivos estáticos."""
    return static_file(filename, "./web")


@route("/chair_status")
def chair_status():
    """GET /chair_status \n
    Permite escribir el estado de una silla."""

    # REST
    response.add_header("Content-Type", "application/json")

    # Conecta con la base de datos
    db = sqlite3.connect('datos.db')
    cur = db.cursor()

    # Consulta base de datos
    data = [*cur.execute("SELECT id, state FROM sillas")]
    sillas = []
    for entry in data:
        sillas.append({"status": "sitting" if entry[1] else "free"})
    return json.dumps(sillas)


@route("/chair", method='POST')
def chair():
    """POST /chair \n
    Permite escribir el estado de una silla."""

    # Conecta con la base de datos
    db = sqlite3.connect('datos.db')
    cur = db.cursor()

    # Escribe base de datos
    estado: bool = request.json["estado"]
    id: int = request.json["id"]
    cur.execute(f"UPDATE sillas SET state={int(estado)} WHERE id={id}")
    db.commit()
    db.close()

    return ""


run(reloader=True, host='0.0.0.0', server='paste', port=8080)