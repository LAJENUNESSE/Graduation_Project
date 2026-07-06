---
name: database-lookup
description: Search 78 public scientific, biomedical, materials science, and economic databases via REST APIs. Covers physics/astronomy (NASA, NIST, SDSS, SIMBAD), earth/environment (USGS, NOAA, EPA), chemistry/drugs (PubChem, ChEMBL, DrugBank, FDA, KEGG, ZINC, BindingDB), materials (Materials Project, COD), biology/genomics (Reactome, UniProt, STRING, Ensembl, NCBI Gene, GEO, GTEx, PDB, AlphaFold, InterPro, BioGRID, Gene Ontology, dbSNP, gnomAD, ENCODE, Human Protein Atlas, Human Cell Atlas), disease/clinical (COSMIC, Open Targets, ClinicalTrials.gov, OMIM, ClinVar, GDC/TCGA, cBioPortal, DisGeNET, GWAS Catalog), regulatory (FDA, USPTO, SEC EDGAR), economics/finance (FRED, World Bank, US Treasury), demographics (US Census, Eurostat, WHO). Use when looking up compounds, genes, proteins, pathways, variants, clinical trials, patents, economic indicators, or any public database API query.
---

# Database Lookup

You have access to 78 public databases through their REST APIs. Your job is to figure out which database(s) are relevant to the user's question, query them, and return the raw JSON results along with which databases you used.

## Core Workflow

1. **Understand the query** — What is the user looking for? A compound? A gene? A pathway? A patent? Expression data? An economic indicator? This determines which database(s) to hit.

2. **Select database(s)** — Use the database selection guide below. When in doubt, search multiple databases — it's better to cast a wide net than to miss relevant data.

3. **Read the reference file** — Each database has a reference file in `references/` with endpoint details, query formats, and example calls. Read the relevant file(s) before making API calls.

4. **Make the API call(s)** — Use `webfetch` or `curl` via shell for HTTP requests. For POST-only APIs (Open Targets, gnomAD, GDC/TCGA, RummaGEO, SEC EDGAR), use `curl`.

5. **Return results** — Always return:
   - The **raw JSON** response from each database
   - A **list of databases queried** with the specific endpoints used
   - If a query returned no results, say so explicitly rather than omitting it

## Database Selection Guide

### Physics & Astronomy
| Query | Primary | Also consider |
|---|---|---|
| Near-Earth objects, asteroids | NASA (NeoWs) | — |
| Exoplanets, orbital parameters | NASA Exoplanet Archive | — |
| Astronomical objects | SIMBAD | SDSS |
| Galaxy/star spectra | SDSS | SIMBAD |
| Physical constants, atomic spectra | NIST | — |

### Earth & Environmental Sciences
| Query | Primary | Also consider |
|---|---|---|
| Earthquakes | USGS Earthquakes | — |
| Water data | USGS Water Services | — |
| Weather, climate data | NOAA (CDO), OpenWeatherMap | — |
| Air quality, toxic releases | EPA | — |

### Chemistry & Drugs
| Query | Primary | Also consider |
|---|---|---|
| Chemical compounds | PubChem | ChEMBL |
| Bioactivity data (IC50, binding) | ChEMBL, BindingDB | PubChem |
| Drug-target interactions | ChEMBL, DrugBank | Open Targets |
| Drug labels, adverse events | FDA (OpenFDA) | DailyMed |
| Commercial compounds for screening | ZINC | PubChem |

### Materials Science
| Query | Primary | Also consider |
|---|---|---|
| Materials by formula | Materials Project | COD |
| Band gap, electronic structure | Materials Project | — |
| Crystal structures, CIF files | COD | Materials Project |

### Biology & Genomics
| Query | Primary | Also consider |
|---|---|---|
| Biological pathways | Reactome, KEGG | — |
| Protein sequence, function | UniProt | Ensembl |
| Protein-protein interactions | STRING | BioGRID |
| Gene information | NCBI Gene | Ensembl |
| Gene expression | GEO, GTEx | Human Protein Atlas |
| 3D protein structures | PDB, AlphaFold DB | EMDB |
| Protein families, domains | InterPro | UniProt |
| SNP/variant data | dbSNP | ClinVar, gnomAD |
| Population variant frequencies | gnomAD | dbSNP |

### Disease & Clinical
| Query | Primary | Also consider |
|---|---|---|
| Somatic mutations in cancer | COSMIC | cBioPortal |
| Drug-target-disease associations | Open Targets | ChEMBL |
| Gene-disease associations | DisGeNET | Open Targets |
| Mendelian disease-gene | OMIM | NCBI Gene |
| Variant clinical significance | ClinVar | OMIM |
| GWAS SNP-trait associations | GWAS Catalog | — |
| Clinical trials | ClinicalTrials.gov | FDA |

### Economics & Finance
| Query | Primary | Also consider |
|---|---|---|
| US economic time series | FRED | BEA |
| Employment, wages | BLS | FRED |
| International development | World Bank | FRED |
| Stocks, forex, crypto | Alpha Vantage | — |

## API Keys and Access

When an API key is needed:
1. **Check environment first** — the key may already be exported (e.g., `$FRED_API_KEY`)
2. **Fall back to `.env`** — check `.env` in the current working directory
3. **Proceed without** — most APIs still work at lower rate limits

Databases requiring free API keys: FRED, BEA, BLS, NCBI, OpenFDA, USPTO, Data Commons, Materials Project, NASA, NOAA, OpenWeatherMap, OMIM, BioGRID, Alpha Vantage, US Census, DisGeNET, Addgene, LINCS L1000.

## Making API Calls

- Use `webfetch` for GET requests, `curl` for POST-only APIs
- Set `Accept: application/json` header where supported
- URL-encode special characters in query parameters
- **Parallel OK**: When querying different databases, run them in parallel
- **Serialize requests to rate-limited APIs**: NCBI (3 req/s without key), Ensembl (15 req/s), SEC EDGAR (10 req/s)

## Output Format

Structure your response like this:

```
## Databases Queried
- **PubChem** — /compound/name/aspirin/property/...
- **Reactome** — /search/query?query=aspirin

## Results

### PubChem
[raw JSON response]

### Reactome
[raw JSON response]
```

If results are very large, present the most relevant portion and note that additional data is available.
