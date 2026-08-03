#pragma once

#include "document.h"

#include <QString>

// Returns true when the file should not be imported into the knowledge base.
bool isImportBlocked(const QString& filePath, QString* reason = nullptr);

// Returns true when the chunk should be excluded from RAG context assembly.
bool isRagExcluded(const DocumentChunk& chunk);

// Returns true for conceptual / architecture questions that benefit from README/docs boost.
bool isConceptualQuery(const QString& question);

// Returns true for external interview/study notes that pollute project conceptual Q&A.
bool isConceptualNoise(const DocumentChunk& chunk);

// README / docs markdown sources preferred for conceptual project questions.
bool isPreferredConceptualSource(const DocumentChunk& chunk);

// Score boost applied only when isConceptualQuery(question) is true.
double conceptualQueryBoost(const QString& question, const DocumentChunk& chunk);
