<?php
/**
 * @file
 * Contains API functions related to generating common/shared data types.
 */

/**
 * Generate Markers (Type=genetic_marker) and optionally variants (Type=SNP).
 *
 * @param $options
 *   An array of options describing what kind and how many markers you
 *   want to generate. The following options are supported:
 *     - num: the number of markers to generate (Default=1)
 *
 * @return
 *   An array keyed with marker feature_id where each item is an array with
 *   a marker and variant object.
 */
function generate_nd_markers($options) {
  $copy_chunk_size = 50000;

  // Define the defaults.
  if (!isset($options['chr'])) return FALSE;
  if (!isset($options['num_per_chr'])) $options['num_per_chr'] = 1;
  if (!isset($options['out_stub'])) $options['out_stub'] = '/tmp/massive_genotype_dataset.drush';
  if (!isset($options['organism_id'])) $options['organism_id'] = trpdata_get_default_organism(array('id only' => TRUE));
  $marker_type = 'Exome Capture';
  $marker_stub = 'exome_' . date('YMd');

  // Retieve the types.
  $marker_type_id = chado_query("SELECT cvterm_id FROM {cvterm} WHERE name='genetic_marker'")->fetchField();
  $options['marker_type_id'] = $marker_type_id;
  $variant_type_id = chado_query("SELECT cvterm_id FROM {cvterm} WHERE name='SNP'")->fetchField();

  $feature_file = $options['out_stub'] . '.feature.csv';
  $FEATURE = fopen($feature_file, 'w');
  print " - Feature table: markers and variants...\n";

  // Now generate variants/markers for that chromosome.
  for($num=1; $num <= $options['num_per_chr']; $num++) {

    // Determine the current position and marker name.
    $offset = rand(1,99);
    $pos = $pos + $offset;
    $variant = $options['chr'] . 'p' . $pos;
    $marker = $options['chr'] . 'p' . $pos . '_' . $marker_stub;

    // One line for the variant.
    fputcsv($FEATURE, array(
      $options['organism_id'], // organism_id
      $variant, // name
      $variant, // uniquename
      $variant_type_id, // type_id
    ));
    // And one line for the marker.
    fputcsv($FEATURE, array(
      $options['organism_id'], // organism_id
      $variant, // name
      $marker, // uniquename
      $marker_type_id, // type_id
    ));

    // If we have reached the size of a chunk then go ahead and copy it into the db.
    if ($num%$copy_chunk_size == 0) {
      fclose($FEATURE);
      _generate_nd_markers_copy_helper($options, $feature_file);
      $FEATURE = fopen($feature_file, 'w');
    }

  }
  fclose($FEATURE);
  _generate_nd_markers_copy_helper($options, $feature_file);
}

function _generate_nd_markers_copy_helper($options, $feature_file) {

  // Now, copy the last chunk of data into the feature table.
  $num_lines = intval(exec("wc -l '$feature_file'"));
  drush_log("    - Generated $num_lines markers and variants",'success');

  $num_lines = chado_query("COPY chado.feature (organism_id, name, uniquename, type_id) FROM '$feature_file' WITH CSV")->rowCount();
  drush_log("    - Saved $num_lines markers and variants to the feature table",'success');

  // Using the COPY File just created, look up the markers just inserted
  // and attach them to the project.
  //-------------------------
  $project_file = $feature_file.'.toproject';

  // We are going to grab the marker uniquenames from the feature_file using LINUX commands.
  // First grab every other row of the feature file (ie: the lines for the markers).
  $command = 'sed -n "0~2p" ' . $feature_file
    // Then grab the third column (ie: the uniquename)
    . ' | cut -d, -f3 '
    // Finally collapse the one marker per line into a comma-separated list.
    . ' | sed -e :a -e "$!N; s/\n/' . "', '" . '/; ta"';
  // Should look like: Chr1p57_exome_2016Oct07', 'Chr1p85_exome_2016Oct07', 'Chr1p145_exome_2016Oct07
  $marker_uniquenames = `$command`;
  $marker_uniquenames = trim($marker_uniquenames);

  // Now use marker uniquenames to Copy them to a file...
  // The following query will select the feaure_id for the uniquenames in $marker_uniquenames
  // Alone it should output: 701639,40
  //                         701641,40
  //                         701643,40
  $sql = "SELECT feature_id||',".$options['project_id']."'
    FROM unnest(ARRAY['$marker_uniquenames']) uniquename
    JOIN {feature} f USING (uniquename)
    WHERE f.type_id=:type AND f.organism_id=:organism";
  // Now use that query with Copy to put the results in a file.
  chado_query("COPY (".$sql.") TO :file", array(':type' => $options['marker_type_id'], ':organism' => $options['organism_id'], ':file' => $project_file));
  // And then into the project_feature table.
  $num_lines = chado_query('COPY project_feature (feature_id, project_id) FROM :file WITH CSV', array(':file' => $project_file))->rowCount();
  drush_log("    - Linked $num_lines markers to the project via the project_feature table.",'success');
}

/**
 * Generate Samples (Type=DNA) and optionally germplasm (Type=Accession).
 *
 * Three Cases:
 *   1) You want generated samples and germplasm but do not want them inserted into the db.
 *        Use $options = array('insert' => FALSE, 'generate germplasm' => TRUE)
 *   2) You want generated samplesm (not inserted) for existing germplasm
 *        Use $options = array('insert' => FALSE, 'generate germplasm' => FALSE)
 *   3) You want generated samples and germplasm with both inserted
 *        Use $options = array('insert' => TRUE, 'generate germplasm' => TRUE)
 *
 * @param $options
 *   An array of options describing what kind and how many stocks you
 *   want to generate. The following options are supported:
 *     - num: the number of stocks to generate (Default=1)
 *     - organism_id: The organism the stocks should belong to.
 *     - generate germplasm: true if you would like to generate random germplasm
 *         as well as samples (Default: FALSE)
 *     - insert: whether or not you want the stocks inserted as you go (Default: FALSE)
 *
 * @return
 *   An array keyed with sample stock_id where each item is an array with
 *   a sample and germplasm object.
 */
function generate_nd_samples($options) {
  $stocks = array();

  // Set defaults.
  if (!isset($options['num'])) $options['num'] = 1;
  if (!isset($options['organism_id'])) $options['organism_id'] = trpdata_get_default_organism(array('id only' => TRUE));
  if (!isset($options['generate germplasm'])) $options['generate germplasm'] = FALSE;
  if (!isset($options['insert'])) $options['insert'] = FALSE;

  // Get Types.
  $sample_type_id = chado_query("SELECT cvterm_id FROM {cvterm} WHERE name='DNA'")->fetchField();
  $germplasm_type_id = chado_query("SELECT cvterm_id FROM {cvterm} WHERE name='Accession'")->fetchField();
  $stockrel_type_id = chado_query("SELECT cvterm_id FROM {cvterm} WHERE name='is_extracted_from'")->fetchField();

  // Get a list of germplasm.
  // If specified, do so by generating random germplasm.
  if ($options['generate germplasm']) {
    $values = array(
      'type_id' => $germplasm_type_id,
      'organism_id' => $options['organism_id'],
    );
    $germplasm = generate_chado_stock($values, $options);
  }
  // Otherwise, simply select a random set from this organism.
  else {
    $sql = 'SELECT * FROM {stock} WHERE organism_id=:organism AND type_id != :type ORDER BY random() LIMIT ' . $options['num'];
    $germplasm = chado_query($sql, array(':organism' => $options['organism_id'], ':type' => $sample_type_id))->fetchAllAssoc('stock_id');
  }

  // Now add samples and relationships for all germplasm.
  foreach ($germplasm as $k => $germ) {

    // First the sample.
    $values = array(
      'type_id' => $sample_type_id,
      'organism_id' => $options['organism_id'],
      'name' => $germ->name . ' Extraction',
      'uniquename' => strtolower(str_replace(' ','_',$germ->name)) . '_extraction_' . date('Ymd'),
    );
    if ($options['insert']) {
      $sample = chado_insert_record('stock',$values);
      $sample = (object) $sample;
    }
    else {
      $sample = (object) $values;
    }

    // Then the relationship: sample => germplasm.
    if ($options['insert'] AND isset($sample->stock_id) AND isset($germ->stock_id)) {
      $values = array(
        'subject_id' => $sample->stock_id,
        'type_id' => $stockrel_type_id,
        'object_id' => $germ->stock_id,
      );
      chado_insert_record('stock_relationship', $values);
    }

    // Then associate with the project.
    if ($options['insert'] AND isset($sample->stock_id) AND isset($options['project_id'])) {
      $values = array(
        'stock_id' => $sample->stock_id,
        'project_id' => $options['project_id'],
      );
      chado_insert_record('project_stock', $values);
    }

    if (isset($sample->stock_id)) {
      $stocks[ $sample->stock_id ] = array(
        'sample' => $sample,
        'germplasm' => $germ
      );
    }
    else {
      $stocks[] = array(
        'sample' => $sample,
        'germplasm' => $germ
      );
    }
  }

  return $stocks;
}
