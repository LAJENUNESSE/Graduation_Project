---
name: literature-review
description: Conduct comprehensive, systematic literature reviews using multiple academic databases (PubMed, arXiv, bioRxiv, Semantic Scholar, etc.). This skill should be used when conducting systematic literature reviews, meta-analyses, research synthesis, or comprehensive literature searches across biomedical, scientific, and technical domains. Creates professionally formatted markdown documents and PDFs with verified citations in multiple citation styles (APA, Nature, Vancouver, etc.).
---

# Literature Review

## Overview

Conduct systematic, comprehensive literature reviews following rigorous academic methodology. Search multiple literature databases, synthesize findings thematically, verify all citations for accuracy, and generate professional output documents in markdown and PDF formats.

## When to Use This Skill

Use this skill when:
- Conducting a systematic literature review for research or publication
- Synthesizing current knowledge on a specific topic across multiple sources
- Performing meta-analysis or scoping reviews
- Writing the literature review section of a research paper or thesis
- Investigating the state of the art in a research domain
- Identifying research gaps and future directions
- Requiring verified citations and professional formatting

## Core Workflow

### Phase 1: Planning and Scoping

1. **Define Research Question**: Use PICO framework (Population, Intervention, Comparison, Outcome) for clinical/biomedical reviews.

2. **Establish Scope and Objectives**: Define clear research questions, determine review type (narrative, systematic, scoping, meta-analysis), set boundaries.

3. **Develop Search Strategy**: Identify 2-4 main concepts, list synonyms and related terms, plan Boolean operators, select minimum 3 complementary databases.

4. **Set Inclusion/Exclusion Criteria**: Date range, language, publication types, study designs. Document all criteria clearly.

### Phase 2: Systematic Literature Search

**Multi-Database Search**: Always search minimum 3 databases for comprehensive coverage.

**Biomedical & Life Sciences**: PubMed, bioRxiv, medRxiv.

**General Scientific Literature**: arXiv (physics, math, CS, q-bio), Semantic Scholar (200M+ papers), Google Scholar.

**Specialized Databases**: Use the `database-lookup` skill or `paper-lookup` skill to access domain-specific databases.

**Document Search Parameters**: Record search strings, date ranges, result counts, and databases used for each search.

### Phase 3: Screening and Selection

1. **Deduplication**: Remove duplicates by DOI (primary) or title (fallback).
2. **Title Screening**: Review titles against criteria, exclude obviously irrelevant.
3. **Abstract Screening**: Apply criteria rigorously, document reasons for exclusion.
4. **Full-Text Screening**: Detailed review, document specific exclusion reasons.

**Create PRISMA Flow Diagram**:
```
Initial search: n = X
├─ After deduplication: n = Y
├─ After title screening: n = Z
├─ After abstract screening: n = A
└─ Included in review: n = B
```

### Phase 4: Data Extraction and Quality Assessment

1. **Extract Key Data**: Study metadata, design/methods, sample size, key findings, limitations, funding sources.

2. **Assess Study Quality**:
   - RCTs: Cochrane Risk of Bias tool
   - Observational studies: Newcastle-Ottawa Scale
   - Systematic reviews: AMSTAR 2

3. **Organize by Themes**: Identify 3-5 major themes, group studies, note patterns and controversies.

### Phase 5: Synthesis and Analysis

**Write Thematic Synthesis** (NOT study-by-study summaries):
- Organize Results by themes or research questions
- Synthesize findings across multiple studies within each theme
- Compare and contrast different approaches and results
- Identify consensus areas and points of controversy
- Highlight the strongest evidence

### Phase 6: Citation Verification

**CRITICAL**: All citations must be verified for accuracy before final submission.

Use the `citation-management` skill to:
1. Verify all DOIs resolve correctly
2. Retrieve metadata from CrossRef
3. Correct any errors
4. Format citations consistently (APA, Nature, Vancouver, Chicago, IEEE)

### Phase 7: Document Generation

1. Write the review following template structure (Introduction → Methods → Results → Discussion → Conclusions).
2. Include PRISMA flow diagram (for systematic reviews).
3. Verify all citations with `citation-management` skill.
4. Generate PDF if needed.

## Prioritizing High-Impact Papers (CRITICAL)

**Citation Count Thresholds:**
| Paper Age | Citation Threshold | Classification |
|-----------|-------------------|----------------|
| 0-3 years | 20+ | Noteworthy |
| 0-3 years | 100+ | Highly Influential |
| 3-7 years | 100+ | Significant |
| 3-7 years | 500+ | Landmark Paper |
| 7+ years | 500+ | Seminal Work |

**Journal Tiers:**
- Tier 1 (Always Prefer): Nature, Science, Cell, NEJM, Lancet, JAMA, PNAS
- Tier 2 (Strong Preference): High-impact specialized journals (IF>10), top conferences (NeurIPS, ICML)
- Tier 3 (Include When Relevant): Specialized journals (IF 5-10)
- Tier 4 (Use Sparingly): Lower-impact venues

## Best Practices

### Search Strategy
1. Use multiple databases (minimum 3)
2. Include preprint servers for latest findings
3. Document everything (search strings, dates, result counts)
4. Test and refine search terms

### Screening
1. Use clear criteria, document before screening
2. Screen systematically: Title → Abstract → Full text
3. Document reasons for exclusions

### Synthesis
1. Organize thematically, NOT by individual studies
2. Synthesize across studies, compare and contrast
3. Be critical of evidence quality
4. Identify research gaps

## Common Pitfalls

1. Single database search — Misses relevant papers
2. No search documentation — Makes review irreproducible
3. Study-by-study summary — Lacks synthesis
4. Unverified citations — Leads to errors
5. Too broad/narrow search — Irrelevant results or missed papers
6. No quality assessment — Treats all evidence equally
7. Outdated search — Field evolves rapidly

## Integration with Other Skills

- **citation-management**: Metadata extraction, citation validation, BibTeX formatting
- **paper-lookup**: Direct API access to 10 academic paper databases
- **database-lookup**: 78 specialized scientific database queries
- **latex-paper-en / latex-thesis-zh**: Export for LaTeX manuscript integration
