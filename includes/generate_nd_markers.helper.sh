#!/bin/bash

# This script is used by the generate_nd_markers() api function.
# It takes a feature file used for COPY purposes as an arguement
# and uses it to look up the feature_ids of the records created.
#
# Assumption: The entire copy set belongs to the same organism and
# has the same type_id.
# Assumption: your chado database is in a 'chado' schema.
#
# Arguments:
#  1. Marker File: contains a list of feature copy records for markers
#  2. Marker Type: the feature.type_id of markers
#  3. Variant File: contains a list of feature copy records for variants
#  4. Variant Type: the feature.type_id of variants
#  5. Organism Id: the feature.organism_id for both markers and variants
#  6. Project ID: the project_id to link markers to
#  7. Feature Relationship Type ID: the feature_relationship.type_id to link variants and markers
#  8. Marker Type type_id: the featureprop.type_id for attaching the marker type to markers
#  9. Marker Type: the string describing the marker type (should be quoted)

MARKER_FILE=$1
TYPE=$2
VARIANT_FILE=$3
VTYPE=$4
ORGANISM=$5
PROJECT_ID=$6
RELTYPE_ID=$7
PROPTYPE_ID=$8
MARKER_TYPE=$9
MARKERNAMES_LIST="$MARKER_FILE.list"
VARIANTNAMES_LIST="$VARIANT_FILE.list"
MARKER_IDS="$MARKER_FILE.feature_ids.list"
VARIANT_IDS="$VARIANT_FILE.feature_ids.list"
TMP_QUERY="$MARKER_FILE.query.tmp"
PROJECT_FILE="$MARKER_FILE.project.csv"
PROP_FILE="$MARKER_FILE.featureprop.csv"
REL_FILE="$MARKER_FILE.relationships.csv"

## MARKERS
#############################

## EXTRACT UNIQUENAMES
# Extract the uniquename (3rd column) and collapse the one
# uniquename per line into a comma-separated list.
# Should look like: Chr1p57_exome_2016Oct07', 'Chr1p85_exome_2016Oct07', 'Chr1p145_exome_2016Oct07
#echo "      - Grabbing the marker uniquenames ";
cut -d, -f3 $MARKER_FILE | sed -e :a -e "$!N; s/\n/', '/; ta" > $MARKERNAMES_LIST

## LOOK-UP FEATURE_IDS
# Generate the query that will be used to look-up the feature_ids
# SELECT feature_id
#  FROM unnest(ARRAY['Chr1p57_exome_2016Oct07', 'Chr1p85_exome_2016Oct07', 'Chr1p145_exome_2016Oct07']) uniquename
#  JOIN chado.feature f USING (uniquename)
#  WHERE f.type_id=1676 AND f.organism_id=5
echo "SELECT feature_id " > $TMP_QUERY
echo "  FROM unnest(ARRAY['$(<$MARKERNAMES_LIST)']) uniquename ">> $TMP_QUERY
echo "  JOIN chado.feature f USING (uniquename) " >> $TMP_QUERY
echo "  WHERE f.type_id=$TYPE AND f.organism_id=$ORGANISM;" >> $TMP_QUERY

# Execute the query we just generated and save the feature_ids into a file.
#echo -n "      - Grabbing the marker feature_ids: ";
PSQL="$(drush sql-connect)"
$PSQL --no-align --tuples-only < $TMP_QUERY > $MARKER_IDS
#wc -l $MARKER_IDS | cut -f1 -d' '

## COMPOSE COPY FILES
# Using the feature_ids we just looked up we would like to create a number of files for
# use with PostgreSQLs COPY Command.

# Project File: link the features to a project
#echo -n "      - Generating the project copy file: ";
sed "s/$/,$PROJECT_ID/" $MARKER_IDS > $PROJECT_FILE
#wc -l $PROJECT_FILE | cut -f1 -d' '

# Feature Property File: link the features to a project
#echo -n "      - Generating the featureprop copy file: ";
sed "s/$/,$PROPTYPE_ID,'$MARKER_TYPE'/" $MARKER_IDS > $PROP_FILE
#wc -l $PROP_FILE | cut -f1 -d' '

## VARIANTS
#############################

## EXTRACT UNIQUENAMES
# Extract the uniquename (3rd column) and collapse the one
# uniquename per line into a comma-separated list.
# Should look like: Chr1p57_exome_2016Oct07', 'Chr1p85_exome_2016Oct07', 'Chr1p145_exome_2016Oct07
#echo "      - Grabbing variant uniquenames ";
cut -d, -f3 $VARIANT_FILE | sed -e :a -e "$!N; s/\n/', '/; ta" > $VARIANTNAMES_LIST

## LOOK-UP FEATURE_IDS
# Generate the query that will be used to look-up the feature_ids
# SELECT feature_id
#  FROM unnest(ARRAY['Chr1p57_exome_2016Oct07', 'Chr1p85_exome_2016Oct07', 'Chr1p145_exome_2016Oct07']) uniquename
#  JOIN chado.feature f USING (uniquename)
#  WHERE f.type_id=1676 AND f.organism_id=5
echo "SELECT feature_id " > $TMP_QUERY
echo "  FROM unnest(ARRAY['$(<$VARIANTNAMES_LIST)']) uniquename ">> $TMP_QUERY
echo "  JOIN chado.feature f USING (uniquename) " >> $TMP_QUERY
echo "  WHERE f.type_id=$VTYPE AND f.organism_id=$ORGANISM;" >> $TMP_QUERY

# Execute the query we just generated and save the feature_ids into a file.
#echo -n "      - Grabbing the variant feature_ids: ";
PSQL="$(drush sql-connect)"
$PSQL --no-align --tuples-only < $TMP_QUERY > $VARIANT_IDS
#wc -l $VARIANT_IDS | cut -f1 -d' '

## RELATIONSHIP
#############################
# Feature Relationship File: links markers to variants.
#echo -n "      - Generating the feature_relationship copy file: ";
paste -d',' $MARKER_IDS $VARIANT_IDS > $REL_FILE
sed -i "s/$/,$RELTYPE_ID/" $REL_FILE
#wc -l $REL_FILE | cut -f1 -d' '
