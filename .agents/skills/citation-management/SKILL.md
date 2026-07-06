---
name: citation-management
description: Comprehensive citation management for academic research. Search Google Scholar and PubMed for papers, extract accurate metadata, validate citations, and generate properly formatted BibTeX entries. This skill should be used when you need to find papers, verify citation information, convert DOIs to BibTeX, or ensure reference accuracy in scientific writing.
---

# Citation Management

## Overview

Manage citations systematically throughout the research and writing process. This skill provides tools and strategies for searching academic databases (Google Scholar, PubMed), extracting accurate metadata from multiple sources (CrossRef, PubMed, arXiv), validating citation information, and generating properly formatted BibTeX entries.

Critical for maintaining citation accuracy, avoiding reference errors, and ensuring reproducible research. Integrates seamlessly with the literature-review skill for comprehensive research workflows.

## When to Use This Skill

Use this skill when:
- Searching for specific papers on Google Scholar or PubMed
- Converting DOIs, PMIDs, or arXiv IDs to properly formatted BibTeX
- Extracting complete metadata for citations (authors, title, journal, year, etc.)
- Validating existing citations for accuracy
- Cleaning and formatting BibTeX files
- Finding highly cited papers in a specific field
- Verifying that citation information matches the actual publication
- Building a bibliography for a manuscript or thesis
- Checking for duplicate citations
- Ensuring consistent citation formatting

## Core Workflow

### Phase 1: Paper Discovery and Search

**Google Scholar**: Most comprehensive coverage across disciplines.

**PubMed**: Biomedical and life sciences literature (35+ million citations). Use MeSH terms for precise searching.

**Best Practices**:
- Use specific, targeted search terms
- Include key technical terms and acronyms
- Filter by recent years for fast-moving fields
- Check "Cited by" to find seminal papers
- Export top results for further analysis

### Phase 2: Metadata Extraction

Convert paper identifiers (DOI, PMID, arXiv ID) to complete, accurate metadata.

Metadata sources:
1. **CrossRef API**: Primary source for DOIs (free, no API key required)
2. **PubMed E-utilities**: Biomedical literature (free, API key recommended)
3. **arXiv API**: Preprints in physics, math, CS, q-bio (free, open access)
4. **DataCite API**: Research datasets, software, other resources

### Phase 3: BibTeX Formatting

Common entry types: `@article`, `@book`, `@inproceedings`, `@incollection`, `@phdthesis`, `@misc`.

**Formatting operations**: Standardize field order, consistent indentation, proper capitalization, standardized author name format, consistent citation key format.

### Phase 4: Citation Validation

Validation checks:
1. **DOI Verification** — DOI resolves correctly via doi.org
2. **Required Fields** — All required fields present for entry type
3. **Data Consistency** — Valid year, numeric volume/number, correct page format
4. **Duplicate Detection** — Same DOI, similar titles, same author/year/title
5. **Format Compliance** — Valid BibTeX syntax, unique citation keys

### Phase 5: Integration with Writing Workflow

This skill complements the `literature-review` skill. Use `literature-review` for systematic search and synthesis, then use `citation-management` to extract and validate all citations.

## Search Strategies

### Google Scholar Best Practices

**Citation Count Thresholds:**
| Paper Age | Citations | Classification |
|-----------|-----------|----------------|
| 0-3 years | 20+ | Noteworthy |
| 0-3 years | 100+ | Highly Influential |
| 3-7 years | 100+ | Significant |
| 3-7 years | 500+ | Landmark Paper |
| 7+ years | 500+ | Seminal Work |
| 7+ years | 1000+ | Foundational |

**Advanced operators**: `"exact phrase"`, `author:lastname`, `intitle:keyword`, `source:journal`, `-exclude`, `OR`, `2020..2024`.

### PubMed Best Practices

Use MeSH terms, field tags ([Title], [Author], etc.), and Boolean operators (AND, OR, NOT) for precise queries.

## Best Practices

1. **Always use DOIs when available** — Most reliable identifier
2. **Verify extracted metadata** — Check author names, journal names, publication year
3. **Handle edge cases** — Preprints (include repository and ID), published preprints (use published version)
4. **Maintain consistency** — Consistent author name format, standardized journal abbreviations
5. **Validate early and often** — Check citations when adding them, validate complete bibliography before submission

## Common Pitfalls

1. Single source bias (only using Google Scholar or PubMed)
2. Accepting metadata blindly without verification
3. Ignoring DOI errors in bibliography
4. Inconsistent formatting across entries
5. Duplicate entries with different citation keys
6. Missing required fields in BibTeX entries
7. Outdated preprints when published version exists

## Integration with Other Skills

- **literature-review**: Multi-database systematic search → citation-management for metadata extraction and validation
- **latex-paper-en / latex-thesis-zh**: Export validated BibTeX for LaTeX manuscripts

## Resources

### External Resources
- Google Scholar: https://scholar.google.com/
- PubMed: https://pubmed.ncbi.nlm.nih.gov/
- CrossRef API: https://api.crossref.org/
- PubMed E-utilities: https://www.ncbi.nlm.nih.gov/books/NBK25501/
- arXiv API: https://arxiv.org/help/api/
- MeSH Browser: https://meshb.nlm.nih.gov/search
- DOI Resolver: https://doi.org/
