import sqlite3
import os
import base64
BASE_DIR = os.path.dirname(os.path.abspath(__file__))
database = os.path.join(BASE_DIR, "AgriGuardEdgeDB.db")

def _bootstrap():
    # Single clean schema (auth shell + §8.1 edge-ingest tables).
    # SQLite runs in WAL mode per docs/architechture.md §9.
    fresh = not os.path.exists(database)
    SQL_DB = sqlite3.connect(database)
    if fresh:
        schema_path = os.path.join(BASE_DIR, "schema.sql")
        if os.path.exists(schema_path):
            with open(schema_path, "r") as f:
                SQL_DB.executescript(f.read())
    SQL_DB.execute("PRAGMA journal_mode=WAL;")
    SQL_DB.commit()
    SQL_DB.close()

_bootstrap()

def SQL_SetData(query, params=None, database=database):
    SQL_DB = sqlite3.connect(database)
    SQL_CURSOR = SQL_DB.cursor()
    if params:
        SQL_CURSOR.execute(query, params)
    else:
        SQL_CURSOR.execute(query)
    SQL_DB.commit()
    SQL_DB.close()

def SQL_Get_ASSOC_DATA(query, params=None, database=database):
    SQL_DB = sqlite3.connect(database)
    SQL_CURSOR = SQL_DB.cursor()
    if params:
        SQL_CURSOR.execute(query, params)
    else:
        SQL_CURSOR.execute(query)
    results = SQL_CURSOR.fetchall()
    results = [dict(zip([desc[0] for desc in SQL_CURSOR.description], row)) for row in results]
    SQL_DB.close()
    return results

def set_profile_pic(userId, image_source, database=database):
    SQL_DB = sqlite3.connect(database)
    SQL_CURSOR = SQL_DB.cursor()

    # Handle both file path and raw bytes
    if isinstance(image_source, str) and os.path.isfile(image_source):
        with open(image_source, "rb") as f:
            image_bytes = f.read()
    elif isinstance(image_source, (bytes, bytearray)):
        image_bytes = image_source
    else:
        raise ValueError("image_source must be a file path or bytes")

    SQL_CURSOR.execute(
        "UPDATE userDetails SET profilePic = ? WHERE id = ?",
        (sqlite3.Binary(image_bytes), userId)
    )
    SQL_DB.commit()
    SQL_DB.close()

def imageBytesToSrc(ImageBytes):
    b64_str = base64.b64encode(ImageBytes).decode("utf-8")
    return f"data:image/png;base64,{b64_str}"
