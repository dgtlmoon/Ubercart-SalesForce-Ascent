<?php

/**
 * Store-wide coupon settings form.
 */
function uc_ascent_salesforce_settings_form() {
  global $base_url;
  ini_set("soap.wsdl_cache_enabled", 0);
  
  // ensure some settings
  if (! variable_get('uc_ascent_salesforce_wsdl_ascent', false) || ! variable_get('uc_ascent_salesforce_wsdl_salesforce', false)) {
    drupal_set_message('WSDL\'s are not configurated correctly, please select an Ascent and Salesforce Enterprise XML WSDL');
  }
  
  $form ['uc_ascent_salesforce_username'] = array (
    '#type' => 'textfield', '#title' => t('SalesForce API Username (email address)'), '#default_value' => variable_get('uc_ascent_salesforce_username', ''), '#required' => true 
  );
  
  $form ['uc_ascent_salesforce_password'] = array (
    '#type' => 'textfield', '#title' => t('SalesForce API Password'), '#default_value' => variable_get('uc_ascent_salesforce_password', ''), '#required' => true 
  );
  
  $form ['uc_ascent_salesforce_token'] = array (
    '#type' => 'textfield', '#title' => t('SalesForce API Token'), '#default_value' => variable_get('uc_ascent_salesforce_token', ''), '#required' => true 
  );
  
  $form ['uc_ascent_salesforce_error_notification_email'] = array (
    '#type' => 'textfield', '#title' => t('Email address to notify on any errors'), '#default_value' => variable_get('uc_ascent_salesforce_error_notification_email', ''), '#required' => true 
  );
  
  $form ['uc_ascent_salesforce_wsdl_salesforce'] = array (
    '#type' => 'select', '#title' => t('WSDL to use for Salesforce Enterprise'), '#description' => t('Chosen from resources/'), '#options' => _get_wsdl_in_resource_directory(), '#required' => true, '#default_value' => variable_get('uc_ascent_salesforce_wsdl_salesforce', false) 
  );
  
  $form ['uc_ascent_salesforce_wsdl_ascent'] = array (
    '#type' => 'select', '#title' => t('WSDL to use for Salesforce Ascent'), '#description' => t('Chosen from resources/'), '#options' => _get_wsdl_in_resource_directory(), '#required' => true, '#default_value' => variable_get('uc_ascent_salesforce_wsdl_ascent', false) 
  );
  
  $form ['uc_ascent_salesforce_run_cron'] = array (
    '#type' => 'checkbox', '#title' => t('Execute transactions with Salesforce/Ascent on cron/hourly'), '#description' => t('If disabled, will just collect items in the queue entered from conditional actions in Ubercart configuration and will not push to Salesforce/Ascent'), '#default_value' => variable_get('uc_ascent_salesforce_run_cron', false) 
  );
  
  return system_settings_form($form);
}

/**
 * Return an array of filenames matching .xml
 */
function _get_wsdl_in_resource_directory() {
  $return = array ();
  $return [] = '<none>';
  if ($handle = opendir(drupal_get_path('module', 'uc_ascent_salesforce') . '/resources/')) {
    while (false !== ($file = readdir($handle))) {
      if (preg_match('/xml$/i', $file)) {
        $return [$file] = $file;
      }
    }
    closedir($handle);
  }
  
  return $return;
}

function uc_ascent_salesforce_queue_info() {
  
  $output = "<h2>Basic statistics</h2>";
  
  // show some general statistics
  $result = db_query("SELECT count(*) as c, queue_function from {uc_sf_queue} GROUP BY queue_function");
  while($row = db_fetch_array($result)) {
    $output.= '<strong>'.$row['queue_function'].'</strong> - '.$row['c'].'<br />';
  }
  $output .="<br/>";
  $output .="<h2>Sanity check on all records</h2>";
  $output .="<i>This checks all transations in the ubercart system are accounted for here, <br /> an each transaction has atleast one SALESORDER_ENTER</i>";
  
  $oldest_order_id = db_result(db_query(" select min(order_id) from uc_sf_queue"));
  $result = db_query("SELECT count(*) as c, qid,queue_function,order_id from uc_sf_queue where order_id >= %d group by queue_function,order_id;", $oldest_order_id);
  $stats = array();
  while($row = db_fetch_array($result)) {
    $stats["order-id-".$row['order_id']][$row['queue_function']]++;
  }
  
  $problem_output=false;
  // now go through each row and make sure it has what we expect
  foreach($stats as $order_id => $info) {
    if($info['ACCOUNT_UPSERT'] == 1 && $info['SALESORDER_ENTER'] == 1 && $info['SALESORDER_ENTER_ORDERLINES'] >= 1) {
      // everything is good
    } else {
      $problem_output.= "<p>Problem with {$order_id}</p>\n";
    }
  }
  
  $output.=$problem_output ? $problem_output : '<p>No errors found</p>';
  
  $output .="<br/>";
  $output.="<h2>Queue status</h2>";
  $header = array (
    array (
    'data' => t('Queue ID'), 'field' => 'qid' 
  ), array (
    'data' => t('Function'), 'field' => 'queue_function' 
  ), array (
    'data' => t('Order ID'), 'field' => 'order_id', 'sort' => 'desc' 
  ), array (
    'data' => t('Queue Status'), 'field' => 'q_status' 
  ), array (
    'data' => t('Processed Time'), 'field' => 'recent_request_time' 
  ), array (
    'data' => t(''),  
  ) 
  );
  
  $result = db_query('SELECT * FROM {uc_sf_queue} order by order_id DESC limit 0,1000');
  $rows = array ();
  while ($queue_item = db_fetch_object($result)) {
    $rows [] = array (
      $queue_item->qid, 
      check_plain($queue_item->queue_function), 
      l($queue_item->order_id, 'admin/store/orders/' . $queue_item->order_id), 
      ! $queue_item->q_status ? t('Waiting') : t('Complete'), 
      $queue_item->request_recent_time, 
      l('view transaction','admin/store/settings/uc_salesforce/queue/'.$queue_item->qid)
    );
  }
  
  if (count($rows)) {
    $output .= theme('table', $header, $rows, array (
      'width' => '100%' 
    ));
    $output .= theme('pager', NULL, 20);
  }
  else {
    $output .= '<p>' . t('There are currently no queued Salesforce/Ascent activities in the system') . '</p>';
  }
  
  $output .= '<p><i>' . t('Events are queued from conditional actions from Ubercart activities') . '</i></p>';
  
  return $output;
}

  

function uc_ascent_salesforce_queue_item_info($qid) {
	
  $query = db_query("SELECT * from {uc_sf_queue_log} WHERE qid=%d ORDER BY request_start_time", $qid);
	
	while($item = db_fetch_array($query)) {

    $form['result_'.$item['id']] = array(  
      '#type' => 'fieldset', 
      '#title' => $item['request_start_time'], 
      '#collapsible' => TRUE, 
      '#collapsed' => FALSE);
  
   $form['result_'.$item['id']]['request'] = array('#type'=>'textarea', 
                           '#cols'=>100, 
                           '#rows'=>15, 
                           '#weight' =>0,
                           '#value'=>$item['request_post'],
                           '#title'=>'XML Request/Post' ); 
  
  $form['result_'.$item['id']]['replay'] = array('#type'=>'textarea', 
                           '#cols'=>100, 
                           '#rows'=>15,
                           '#weight' =>1,
                           '#value'=>$item['request_reply'],
                           '#title'=>'XML Reply' ); 
	}
	
  return drupal_render($form);
}

function uc_ascent_salesforce_configuration_check() {
  
  $sf = uc_ascent_connect(true);
  
  if($sf) {
    $output .= "<p>SalesForce/Ascent connection <strong>established</strong></p>";
  } else {
    $output .= "<p>SalesForce/Ascent connection <strong>NOT established</strong></p>";
    return $output;
  }
  $output.="<h2>Product ID check</h2>";
  // add the lineitem product code (this is also added as a lineitem)
  
  $code_id =_uc_ascent_get_shippinglineitem_product_code();
  $id =  _uc_ascent_query_table($sf, 'PBSI__PBSI_item__c', $code_id ); 
  $rows[] = array($code_id, $code_id, 'n/a', $id);
  
  $query = db_query("SELECT nid, vid from {node} where type='product'");
  while ($node_row = db_fetch_array($query)) {
    $node = node_load(array('nid'=>$node_row['nid'], 'vid'=>$node_row['vid']));
    $id = _uc_ascent_query_table($sf, 'PBSI__PBSI_item__c', $node->model );    
    $row = array(l($node->title,'node/'.$node->nid), $node->model, $node->status == 1 ? "Published" : "Unpublished", $id);
    $rows[] = $row;
  }
  
  $output.=theme('table', array('Node Title', 'Model / SKU','Node Status' ,'SalesForce ID'), $rows);
  
  $SF_table_value_lookups = array();
  $SF_table_value_lookups[]["c2g__codaGeneralLedgerAccount__c"] = "Accounts Payable";
  $SF_table_value_lookups[]["c2g__codaGeneralLedgerAccount__c"] = "Accounts Receivable";
  $SF_table_value_lookups[]["c2g__CODADimension1__c"] = "Consumer Products";
  $SF_table_value_lookups[]["c2g__codaTaxCode__c"] = "FRE";
  $SF_table_value_lookups[]["c2g__codaTaxCode__c"] = "EXP";
  
  $output.="<h2>Check custom field values exist</h2>";
  $rows = array();
  foreach($SF_table_value_lookups as $lookup) {
    foreach($lookup as $field => $value) {
      try {
        $id = _uc_ascent_query_table($sf, $field, $value );
      } catch (SoapFault $err) {
        $id = '<span style="color:#ee0000">&lt;field not configured/available&gt;</span>';
      }
      $row = array($field, $value, $id ? $id : '<span style="color:#ee0000">&lt;value missing&gt;</span>');
      $rows[] = $row;  
    }
  } 
  
  $output.=theme('table', array('Field to check', 'Value to check', 'SalesForce ID'), $rows);
  $sf->logout();
  
  return $output;
}


