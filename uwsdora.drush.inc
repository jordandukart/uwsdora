<?php

/**
 * @file
 * Drush command/hook implementation for updating Handles for UWS.
 */

/**
 * Implements hook_drush_command().
 */
function uwsdora_drush_command() {
  $commands = array();

  $commands['uwsdora-update-handles'] = array(
    'description' => dt('Update existing Handles to point at new Islandora object URLs. Subsquent runs of this command should only go back and update Handles that do not return a success code from the initial run.'),
    'drupal dependencies' => array(
      'islandora',
      'islandora_handle',
      'uwsdora',
    ),
    'examples' => array(
      'drush -u 1 uwsdora-update-handles' => dt('Updating existing Handles.'),
    ),
    'bootstrap' => DRUSH_BOOTSTRAP_DRUPAL_LOGIN,
  );
  $commands['uwsdora-update-modscollection'] = array(
    'description' => dt('Strip off the superfluous ModsCollection wrapper from the UWS Thesis Collection'),
    'drupal dependencies' => array(
      'islandora',
      'uwsdora',
    ),
    'examples' => array(
      'drush -u 1 uwsdora-update-modscollection' => dt('Updating MODS records.'),
    ),
    'boostrap' => DRUSH_BOOTSTRAP_DRUPAL_LOGIN,
  );
  return $commands;
}

/**
 * Command callback to update Handles to point to new URLs.
 */
function drush_uwsdora_update_handles() {
  batch_set(uwsdora_update_handles_create_batch());
  drush_backend_batch_process();
}

/**
 * Constructs a batch used to update things via Drush.
 */
function uwsdora_update_handles_create_batch() {
  return array(
    'operations' => array(
      array('uwsdora_update_handles_batch_operation', array()),
    ),
    'title' => t('Updating Handles for objects...'),
    'init_message' => t('Preparing to update Handles.'),
    'progress_message' => t('Time elapsed: @elapsed <br/>Estimated time remaning @estimate.'),
    'error_message' => t('An error has occurred.'),
    'file' => drupal_get_path('module', 'uwsdora') . '/uwsdora.drush.inc',
  );
}

/**
 * Constructs and performs the citation batch operation.
 *
 * @param array $context
 *   The context of the Drupal batch.
 */
function uwsdora_update_handles_batch_operation(&$context) {
  module_load_include('inc', 'islandora_handle', 'includes/handle');
  $handle_update = 10;
  $result = db_query('SELECT COUNT(*) FROM {uwsdora_handles} WHERE updated = :updated', array(':updated' => 0));
  $total_results = $result->fetchField();

  $sandbox = &$context['sandbox'];
  if (!isset($sandbox['offset'])) {
    $sandbox['offset'] = 0;
    $sandbox['total'] = $total_results;
    if ($sandbox['total'] == 0) {
      return;
    }
  }

  $context['message'] = t('Processing results @start to @end.', array(
    '@start' => $sandbox['offset'],
    '@end' => min($sandbox['offset'] + $handle_update, $sandbox['total']),
  ));
  $op_results = db_select('uwsdora_handles', 'u')
    ->fields('u', array('handle', 'pid', 'ds'))
    ->condition('updated', 0, '=')
    ->range($sandbox['offset'], $handle_update)
    ->execute();
  foreach ($op_results as $result) {
    // We need to grok out the prefix and suffix from the stored URL.
    $handle = str_replace('http://handle.uws.edu.au:8081/', '', $result->handle);
    $target = url("islandora/object/{$result->pid}", array(
      'language' => (object) array('language' => FALSE),
      'absolute' => TRUE,
    ));
    $response = islandora_handle_update_handle_url($handle, $target);
    if ($response->code == 201 || $response->code == 204) {
      // Success we updated the Handle so let's update the value in the db
      // and log accordingly.
      if ($response->code == 201) {
        drush_log(dt('Successfully created a new Handle (@handle) for @pid from the @ds datastream.',
          array(
            '@handle' => $handle,
            '@pid' => $result->pid,
            '@ds' => $result->ds,
          )
        ), 'success');
      }
      else {
        drush_log(dt('Successfully updated the Handle (@handle) for @pid from the @ds datastream.',
          array(
            '@handle' => $handle,
            '@pid' => $result->pid,
            '@ds' => $result->ds,
          )
        ), 'success');
      }
      db_update('uwsdora_handles')
        ->fields(array(
          'updated' => 1,
        ))
        ->condition('pid', $result->pid, '=')
        ->condition('ds', $result->ds, '=')
        ->execute();
    }
    else {
      // Oh no! Something broke, we should log it.
      drush_log(dt('Unable to update the Handle (@handle) for @pid from the @ds datastream. Response code: @code. Error: @error',
        array(
          '@handle' => $handle,
          '@pid' => $result->pid,
          '@ds' => $result->ds,
          '@code' => $response->code,
          '@error' => $response->error,
        ),
      'error'));
    }
  }
  $sandbox['offset'] += $handle_update;
  $context['finished'] = $sandbox['offset'] / $sandbox['total'];
}

/**
 * Command callback to remove the MODSCollection element from MODS.
 */
function drush_uwsdora_update_modscollection() {
  batch_set(uwsdora_update_modscollection_create_batch());
  drush_backend_batch_process();
}

/**
 * Constructs a batch used to update things via Drush.
 */
function uwsdora_update_modscollection_create_batch() {
  return array(
    'operations' => array(
      array('uwsdora_update_modscollection_batch_operation', array()),
    ),
    'title' => t('Updating MODS for Thesis Collection...'),
    'init_message' => t('Preparing to update MODS records.'),
    'progress_message' => t('Time elapsed: @elapsed <br/>Estimated time remaning @estimate.'),
    'error_message' => t('An error has occurred.'),
    'file' => drupal_get_path('module', 'uwsdora') . '/uwsdora.drush.inc',
  );
}

/**
 * Constructs and performs the modsCollection update batch operation.
 *
 * @param array $context
 *   The context of the Drupal batch.
 */
function uwsdora_update_modscollection_batch_operation(&$context) {
  $mods_update = 10;
  $query = <<<EOQ
SELECT ?pid FROM <#ri>
WHERE {
  ?pid <fedora-rels-ext:isMemberOfCollection> <info:fedora/uws:thesisCollection> ;
}
EOQ;
  $connection = islandora_get_tuque_connection();
  $sandbox = &$context['sandbox'];
  if (!isset($sandbox['offset'])) {
    $sparql_count = $connection->repository->ri->countQuery($query, 'sparql');
    $sandbox['offset'] = 0;
    $sandbox['total'] = $sparql_count;
    if ($sandbox['total'] === 0) {
      return;
    }
  }

  $context['message'] = t('Processing results @start to @end.', array(
    '@start' => $sandbox['offset'],
    '@end' => min($sandbox['offset'] + $mods_update, $sandbox['total']),
  ));

  $offset_start = $sandbox['offset'];
  $query .= "
  LIMIT $mods_update
  OFFSET $offset_start
  ";
  module_load_include('inc', 'islandora', 'includes/derivatives');
  $results = $connection->repository->ri->sparqlQuery($query);
  foreach ($results as $result) {
    $object = islandora_object_load($result['pid']['value']);
    if ($object && isset($object['MODS'])) {
      $dom = new DOMDocument();
      $dom->loadXML($object['MODS']->content);
      if ($dom->documentElement->tagName == 'modsCollection') {
        $new_dom = new DOMDocument('1.0', 'UTF-8');
        $new_dom->formatOutput = TRUE;
        $new_node = $new_dom->importNode($dom->getElementsByTagName('mods')->item(0), TRUE);
        $new_dom->appendChild($new_node);
        $object['MODS']->content = $new_dom->saveXML();
        drush_log(dt('Updated the MODS record for @object!', array('@object' => $result['pid']['value'])), 'success');
      }
      else {
        drush_log(dt('The object @object did not have the modsCollection wrapper on it', array('@object' => $result['pid']['value'])), 'notice');
      }
    }
    else {
      drush_log(dt("The object @object does not exist or it does not have a MODS datastream!", array('@object' => $result['pid']['value'])), 'error');
    }
  }
  $sandbox['offset'] += $mods_update;
  $context['finished'] = $sandbox['offset'] / $sandbox['total'];
}