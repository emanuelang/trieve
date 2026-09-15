# Informe Knowledge V1

## Objetivo

La V1 del modulo `knowledge` agrega una primera capa de mapa de conocimiento organizacional encima del RAG existente. El objetivo es convertir texto extraido desde archivos, especialmente imagenes procesadas por OCR, en relaciones estructuradas y trazables.

El RAG sigue resolviendo busqueda semantica por fragmentos. El modulo `knowledge` agrega estructura:

```text
Nodo -> relacion -> Nodo
```

Ejemplo:

```text
Ventas --responsable_de--> cargar los datos del cliente en el CRM
emitir una factura --requiere--> validar el CUIT del cliente
```

## Arquitectura General

El flujo de indexacion queda asi:

```text
Archivo
 -> Extractor/OCR
 -> FileDocument.contexts()
 -> RagModule.indexDocument()
 -> KnowledgeModule.indexDocument()
 -> repositorio RAG + repositorio Knowledge
```

El flujo de pregunta queda asi:

```text
Pregunta
 -> RagQueryService.retrieve()
 -> ContextRanker.rankAndLimit()
 -> KnowledgeQueryService.retrieveRelated()
 -> PromptBuilder.build()
 -> ILlmClient.generate()
 -> AnswerResult
```

La respuesta final del LLM recibe dos bloques:

```text
Contexto recuperado:
fragmentos textuales del RAG

Contexto estructurado del mapa de conocimiento:
relaciones extraidas desde los documentos
```

## Clases Principales

### `KnowledgeNode`

Representa una entidad del mapa.

Tipos iniciales:

```text
Area
Role
Process
Action
Rule
Document
System
Concept
```

Campos relevantes:

```text
id
type
name
confidence
status
evidence
```

### `KnowledgeEdge`

Representa una relacion entre dos nodos.

Relaciones iniciales:

```text
responsable_de
requiere
usa_sistema
pertenece_a
bloquea
aprueba
evidencia
```

Campos relevantes:

```text
fromNodeId
toNodeId
type
confidence
status
evidence
```

### `KnowledgeEvidence`

Guarda trazabilidad de cada nodo o relacion.

Campos:

```text
text
sourceFile
source
detectedLanguage
languageConfidence
contextIndex
authority
```

Esto permite saber de que documento, contexto e idioma salio una relacion.

### `KnowledgeRule`

Representa una regla empresarial detectada como objeto.

Ejemplo:

```text
subject: emitir una factura
requiredAction: validar el CUIT del cliente
confidence: 0.90
status: accepted
```

En esta V1 las reglas se detectan junto con relaciones de tipo `requiere` o `aprueba`.

### `RuleBasedKnowledgeExtractor`

Extrae conocimiento desde `FileDocument.contexts()` aplicando patrones linguisticos.

Responsabilidades:

```text
- dividir texto en oraciones
- elegir patrones segun idioma detectado
- aplicar regex por idioma
- crear nodos
- crear relaciones
- crear reglas cuando corresponde
- guardar evidencia exacta
```

### `KnowledgeModule`

Es la fachada principal del modulo.

Expone:

```cpp
KnowledgeIndexingResult indexDocument(const FileDocument& document) const;
std::vector<RetrievedKnowledge> retrieveRelated(const std::string& query, std::size_t maxResults) const;
```

### `InMemoryKnowledgeRepository`

Repositorio inicial en memoria.

Responsabilidades:

```text
- deduplicar nodos por id
- deduplicar relaciones por id
- acumular evidencia
- buscar relaciones relacionadas por coincidencia lexical simple
```

### `KnowledgeQueryService`

Servicio usado por el modulo de respuestas para consultar relaciones relevantes.

## Soporte Multiidioma

Los chunks pueden venir en distintos idiomas. La V1 usa el idioma ya detectado en `ExtractedSegment`:

```cpp
detectedLanguage
languageConfidence
```

La estrategia es:

```text
1. Si el idioma es confiable, aplicar patrones de ese idioma.
2. Si el idioma es dudoso, aplicar todos los patrones como fallback.
3. Guardar idioma y confianza dentro de la evidencia.
4. Normalizar todas las relaciones al mismo modelo interno.
```

Idiomas iniciales:

```text
es: espanol
en: ingles
pt: portugues sin acentos para OCR/regex V1
```

Ejemplos equivalentes:

```text
Ventas es responsable de cargar datos.
Sales is responsible for entering data.
Vendas e responsavel por inserir dados.
```

Los tres se guardan como:

```text
Area --responsable_de--> Action
```

## Patrones Incluidos

### Espanol

```text
X es responsable de Y
X debe Y
Antes de X, se debe Y
X requiere Y
X necesita Y
No se puede X sin Y
X usa Y
X utiliza Y
X requiere aprobacion de Y
X debe ser aprobado por Y
```

### Ingles

```text
X is responsible for Y
X must Y
Before X, Y must be done/completed/validated/confirmed
X requires Y
X needs Y
Cannot X without Y
X uses Y
X is registered in Y
X requires approval from Y
X must be approved by Y
```

### Portugues

```text
X e responsavel por Y
X deve Y
Antes de X, deve-se Y
X requer Y
X precisa de Y
Nao se pode X sem Y
X usa Y
X utiliza Y
X requer aprovacao de Y
X deve ser aprovado por Y
```

## Integracion Con `IndexingOrchestrator`

`IndexingOrchestrator::indexFile()` ahora ejecuta:

```cpp
file.setRagIndexingResult(ragModule_.indexDocument(file));
file.setKnowledgeIndexingResult(knowledgeModule_.indexDocument(file));
```

Esto significa que cada archivo indexado genera:

```text
- indice semantico RAG
- mapa de conocimiento
```

## Integracion Con `AnswerModule`

`AnswerModule` ahora recibe:

```cpp
RagQueryService
KnowledgeQueryService
ContextRanker
PromptBuilder
ILlmClient
```

Durante una pregunta:

```cpp
const auto retrieved = ragQueryService_.retrieve(...);
const auto relatedKnowledge = knowledgeQueryService_.retrieveRelated(...);
```

Luego ambos se envian al `PromptBuilder`.

## Integracion Con `PromptBuilder`

El prompt ahora incluye relaciones estructuradas:

```text
[K1] Ventas(Area) --responsable_de--> cargar datos(Action)
Evidencia: Ventas es responsable de cargar datos
```

Tambien incluye:

```text
score
confidence
lang
languageConfidence
```

## CLI De Prueba

Comandos disponibles:

```powershell
semantic_fs_backend ingest <file_path>
semantic_fs_backend ingest-folder <folder_path>
semantic_fs_backend graph-search <question>
semantic_fs_backend ask <question> [file_path]
```

Ejemplo:

```powershell
cd C:\Users\54343\OneDrive\Desktop\trieve\Backend
.\build\Debug\semantic_fs_backend.exe ingest-folder .\demo_documents
.\build\Debug\semantic_fs_backend.exe graph-search "invoice tax ID"
.\build\Debug\semantic_fs_backend.exe ask "Puedo facturar sin CUIT validado?"
```

## Documentos Demo

Se agregaron imagenes que simulan documentos impresos para trabajar con el extractor OCR actual:

```text
Backend/demo_documents/alta_cliente.png
Backend/demo_documents/descuentos.png
Backend/demo_documents/soporte.png
Backend/demo_documents/customer_onboarding_en.png
Backend/demo_documents/discount_policy_en.png
Backend/demo_documents/onboarding_cliente_pt.png
```

## Limites De La V1

Esta V1 es intencionalmente simple:

```text
- no usa Ollama para extraer relaciones complejas todavia
- no persiste el grafo en SQLite todavia
- no resuelve conflictos todavia
- no fusiona sinonimos entre idiomas
- no normaliza acentos en portugues/espanol
- la busqueda del grafo es lexical, no vectorial
```

## Siguiente Paso Recomendado

La V1.2 deberia agregar una interfaz de extractor por agente:

```cpp
class IKnowledgeExtractor
```

Implementaciones:

```text
RuleBasedKnowledgeExtractor
OllamaKnowledgeExtractor
CustomAgentKnowledgeExtractor
```

Politica:

```text
Rule-based = relaciones explicitas de alta confianza.
Ollama/agente = relaciones candidatas, mas ricas, siempre con evidencia.
Conflictos = no se sobrescriben; se marcan para revision.
```
